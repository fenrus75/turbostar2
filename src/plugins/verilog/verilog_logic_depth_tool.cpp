#include "plugins/verilog/verilog_logic_depth_tool.h"
#include "plugins/verilog/netlist_graph_analyzer.h"
#include "fs_utils.h"

#include <filesystem>
#include <fstream>
#include <format>
#include <set>

namespace tools
{

verilog_logic_depth_tool::verilog_logic_depth_tool(verilog_logic_depth_args args)
	: agentlib::llm_tool_action(std::format("Analyzing logic depth for {}",
	                                       args.requested_module.empty() ? args.requested_file : args.requested_module)),
	  args_(std::move(args))
{
}

bool verilog_logic_depth_tool::validate_runtime(const agentlib::tool_context &/*ctx*/, std::string &/*out_error*/) const
{
	return true;
}

std::string verilog_logic_depth_tool::execute(agentlib::tool_context &ctx)
{
	verilog::netlist_graph_analyzer analyzer;
	std::string err;
	bool loaded = false;

	if (!args_.safe_netlist.empty()) {
		loaded = analyzer.load_from_file(args_.safe_netlist, err, args_.requested_module);
		if (!loaded) {
			set_failure(ctx, err);
			return std::format("Error loading netlist '{}': {}", args_.safe_netlist, err);
		}
	} else {
		std::vector<std::string> sources;
		if (!args_.safe_file.empty()) {
			std::filesystem::path fp(args_.safe_file);
			// Gather synthesizable sibling RTL files from the enclosing directory tree
			std::filesystem::path scan_root = fp.parent_path();
			if (scan_root.has_parent_path() && scan_root.filename() != "sv") {
				scan_root = scan_root.parent_path();
			}
			try {
				for (const auto &entry : std::filesystem::recursive_directory_iterator(scan_root)) {
					if (entry.is_regular_file() && verilog::netlist_graph_analyzer::is_synthesis_source_file(entry.path())) {
						sources.push_back(entry.path().string());
					}
				}
			} catch (...) {
			}
			if (sources.empty()) {
				sources.push_back(args_.safe_file);
			}
		}

		loaded = analyzer.synthesize_and_load(args_.requested_module, sources, args_.target_mode, err);
		if (!loaded) {
			set_failure(ctx, err);
			return std::format("Synthesis/analysis failed for module '{}': {}", args_.requested_module, err);
		}
	}

	std::string md_tables = analyzer.generate_timing_markdown(args_.target_mode);

	std::string sidecar_note;
	if (args_.write_sidecar) {
		std::string sidecar_path;
		if (!args_.safe_file.empty()) {
			std::filesystem::path p(args_.safe_file);
			sidecar_path = p.replace_extension(".timing.md").string();
		} else {
			std::string mod_name = analyzer.get_module_name().empty() ? "module" : analyzer.get_module_name();
			sidecar_path = std::format("{}.timing.md", mod_name);
		}

		std::string safe_write_path;
		std::string sec_err;
		if (ctx.fs_security.validate_access(sidecar_path, agentlib::access_type::write, safe_write_path, sec_err)) {
			std::ofstream ofs(safe_write_path);
			if (ofs.is_open()) {
				ofs << md_tables;
				sidecar_note = std::format("\n> [!TIP]\n> Updated timing sidecar written to `{}`.\n\n", sidecar_path);
			} else {
				sidecar_note = std::format("\n> [!WARNING]\n> Failed to write timing sidecar to `{}`.\n\n", sidecar_path);
			}
		}
	}

	auto format_cat = [](verilog::path_category cat) -> std::string {
		switch (cat) {
		case verilog::path_category::reg_to_reg: return "Reg -> Reg";
		case verilog::path_category::in_to_reg: return "In -> Reg";
		case verilog::path_category::reg_to_out: return "Reg -> Out";
		case verilog::path_category::in_to_out: return "In -> Out";
		}
		return "";
	};

	auto format_cats = [&](std::span<const verilog::path_category> cats) -> std::string {
		std::string s;
		for (size_t i = 0; i < cats.size(); ++i) {
			if (i > 0) s += ", ";
			s += format_cat(cats[i]);
		}
		return s;
	};

	auto format_hops_chain = [&](std::span<const verilog::path_hop> hops) -> std::string {
		struct collapsed_stage {
			std::string src_location;
			size_t count{0};
			std::vector<std::string> input_hints;
			std::string out_net;
		};
		std::vector<collapsed_stage> stages;

		for (const auto &hop : hops) {
			if (!stages.empty() && !hop.src_location.empty() && stages.back().src_location == hop.src_location) {
				stages.back().count++;
				if (!hop.input_hints.empty()) {
					stages.back().input_hints.push_back(hop.input_hints);
				}
				if (!hop.net_name.empty()) {
					stages.back().out_net = hop.net_name;
				}
			} else {
				collapsed_stage st;
				st.src_location = hop.src_location;
				st.count = 1;
				if (!hop.input_hints.empty()) {
					st.input_hints.push_back(hop.input_hints);
				}
				st.out_net = hop.net_name;
				stages.push_back(std::move(st));
			}
		}

		std::string chain_md;
		for (size_t s_idx = 0; s_idx < stages.size(); ++s_idx) {
			const auto &st = stages[s_idx];
			std::string stage_units;
			if (args_.target_mode == "Generic Gates") {
				stage_units = (st.count == 1) ? "1 gate" : std::format("{} gates", st.count);
			} else {
				stage_units = (st.count == 1) ? "1 LUT" : std::format("{} LUTs", st.count);
			}

			std::string loc_str = st.src_location.empty() ? "[logic]" : std::format("`{}`", st.src_location);
			std::string hint_str;
			if (!st.input_hints.empty()) {
				std::vector<std::string> token_hints;
				for (const auto &h : st.input_hints) {
					size_t pos = 0;
					while (pos < h.size()) {
						size_t comma = h.find(',', pos);
						std::string sub = (comma == std::string::npos) ? h.substr(pos) : h.substr(pos, comma - pos);
						while (!sub.empty() && sub.front() == ' ') sub.erase(sub.begin());
						while (!sub.empty() && sub.back() == ' ') sub.pop_back();
						if (!sub.empty()) {
							token_hints.push_back(sub);
						}
						if (comma == std::string::npos) break;
						pos = comma + 1;
					}
				}
				if (!token_hints.empty()) {
					std::string compacted = verilog::netlist_graph_analyzer::compact_signal_names(token_hints);
					if (!compacted.empty()) {
						hint_str = std::format(", from {}", compacted);
					}
				}
			}

			std::string out_str;
			if (!st.out_net.empty() && st.out_net.find('$') == std::string::npos &&
			    !st.out_net.starts_with("new_n") && !st.out_net.starts_with("wire_")) {
				out_str = std::format(" -> `{}`", st.out_net);
			}

			chain_md += std::format("  {}. {} ({}{}){}\n", s_idx + 1, loc_str, stage_units, hint_str, out_str);
		}
		return chain_md;
	};

	std::string paths_md;
	std::string unit_str = (args_.target_mode == "Generic Gates") ? "gates" : "LUTs";

	if (args_.group_by_sink) {
		auto bottlenecks = analyzer.get_worst_bottlenecks(std::nullopt, args_.top_k);
		if (!bottlenecks.empty()) {
			paths_md += "\n## Worst Combinational Paths (Grouped by Bottleneck Sink)\n\n";

			const auto &top = bottlenecks.front();
			std::string top_cats = format_cats(top.categories);
			std::string top_unit = (args_.target_mode == "Generic Gates") ? ((top.depth == 1) ? "gate" : "gates") : ((top.depth == 1) ? "LUT" : "LUTs");
			if (top.sources.size() > 1) {
				paths_md += std::format("**Critical Path Summary**: {}, {} {} (converging on `{}` from {} source signals)\n\n",
				                        top_cats, top.depth, top_unit, top.endpoint, top.sources.size());
			} else if (top.sources.size() == 1) {
				paths_md += std::format("**Critical Path Summary**: {}, {} {} (from `{}` to `{}`)\n\n",
				                        top_cats, top.depth, top_unit, top.sources.front().name, top.endpoint);
			}

			for (size_t i = 0; i < bottlenecks.size(); ++i) {
				const auto &b = bottlenecks[i];
				std::string cat_str = format_cats(b.categories);
				std::string depth_unit = (args_.target_mode == "Generic Gates") ? ((b.depth == 1) ? "gate" : "gates") : ((b.depth == 1) ? "LUT" : "LUTs");

				if (b.sources.size() > 1) {
					paths_md += std::format("### Path #{} [{}] Depth: {} {} — Converging on `{}`\n",
					                        i + 1, cat_str, b.depth, depth_unit, b.endpoint);
					std::string src_list = verilog::netlist_graph_analyzer::format_source_ranges(b.sources);
					paths_md += std::format("- **Sources** ({} source bits, all converge here): {}\n",
					                        b.sources.size(), src_list);
				} else if (b.sources.size() == 1) {
					paths_md += std::format("### Path #{} [{}] Depth: {} {}\n", i + 1, cat_str, b.depth, depth_unit);
					paths_md += std::format("- **Start**: `{}` {}\n", b.sources.front().name,
					                        b.sources.front().src_location.empty() ? "" : std::format("(`{}`)", b.sources.front().src_location));
				} else {
					paths_md += std::format("### Path #{} [{}] Depth: {} {}\n", i + 1, cat_str, b.depth, depth_unit);
				}

				paths_md += format_hops_chain(b.hops);

				paths_md += std::format("- **End**: `{}` {}\n", b.endpoint,
				                        b.end_src.empty() ? "" : std::format("(`{}`)", b.end_src));
				if (!b.note.empty()) {
					paths_md += std::format("  > Note: {}\n", b.note);
				}
				paths_md += "\n";
			}
		}
	} else {
		auto worst_paths = analyzer.get_worst_paths(std::nullopt, args_.top_k);
		if (!worst_paths.empty()) {
			paths_md += "\n## Worst Combinational Paths\n\n";

			const auto &top = worst_paths.front();
			std::string top_cat_str = format_cat(top.category);
			std::string top_unit = (args_.target_mode == "Generic Gates") ? ((top.depth == 1) ? "gate" : "gates") : ((top.depth == 1) ? "LUT" : "LUTs");
			paths_md += std::format("**Critical Path Summary**: {}, {} {} (from `{}` to `{}`)\n\n",
			                        top_cat_str, top.depth, top_unit, top.startpoint, top.endpoint);

			for (size_t i = 0; i < worst_paths.size(); ++i) {
				const auto &p = worst_paths[i];
				std::string cat_str = format_cat(p.category);
				std::string depth_unit = (args_.target_mode == "Generic Gates") ? ((p.depth == 1) ? "gate" : "gates") : ((p.depth == 1) ? "LUT" : "LUTs");

				paths_md += std::format("### Path #{} [{}] Depth: {} {}\n", i + 1, cat_str, p.depth, depth_unit);
				paths_md += std::format("- **Start**: `{}` {}\n", p.startpoint,
				                        p.start_src.empty() ? "" : std::format("(`{}`)", p.start_src));

				paths_md += format_hops_chain(p.hops);

				paths_md += std::format("- **End**: `{}` {}\n", p.endpoint,
				                        p.end_src.empty() ? "" : std::format("(`{}`)", p.end_src));
				if (!p.note.empty()) {
					paths_md += std::format("  > Note: {}\n", p.note);
				}
				paths_md += "\n";
			}
		}
	}

	set_success(ctx);
	return md_tables + sidecar_note + paths_md;
}

} // namespace tools
