#include "plugins/verilog/verilog_logic_depth_tool.h"
#include "plugins/verilog/netlist_graph_analyzer.h"
#include "plugins/verilog/verilog_module_scanner.h"
#include "plugins/verilog/fpga_project_settings.h"
#include "plugins/verilog/nextpnr_timing_analyzer.h"
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

	std::string target_mode = args_.target_mode;

	if (!args_.safe_netlist.empty()) {
		loaded = analyzer.load_from_file(args_.safe_netlist, err, args_.requested_module);
		if (!loaded) {
			set_failure(ctx, err);
			return std::format("Error loading netlist '{}': {}", args_.safe_netlist, err);
		}
	} else {
		std::string project_root = ctx.fs_security.get_working_directory().string();
		if (project_root.empty()) {
			project_root = ".";
		}

		verilog::fpga_target_config cfg;
		std::string load_err;
		verilog::fpga_project_settings::load_from_project(project_root, cfg, load_err);

		if (args_.requested_module.empty()) {
			if (!cfg.top_module.empty()) {
				args_.requested_module = cfg.top_module;
			} else {
				set_failure(ctx, "No module name specified and no top_module configured in project settings");
				return "Error: No module name specified and no top_module configured in turbostar.json. "
				       "Please pass 'module' parameter or configure 'top_module' via verilog_project_settings.";
			}
		}

		if (target_mode == "Generic LUT4" && cfg.target == "ice40") {
			target_mode = "iCE40";
		}

		std::vector<std::string> sources;
		if (!args_.safe_file.empty()) {
			std::filesystem::path fp(args_.safe_file);
			// Gather synthesizable sibling RTL files from the enclosing directory tree
			std::filesystem::path scan_root = fp.parent_path();
			if (scan_root.has_parent_path() && scan_root.filename() != "sv") {
				scan_root = scan_root.parent_path();
			}
			sources = verilog::verilog_module_scanner::find_synthesizable_files(scan_root);
			if (sources.empty()) {
				sources.push_back(args_.safe_file);
			}
		} else {
			std::filesystem::path scan_root = project_root;
			if (!cfg.sv_path.empty()) {
				scan_root = std::filesystem::path(project_root) / cfg.sv_path;
			}
			sources = verilog::verilog_module_scanner::find_synthesizable_files(scan_root);
		}

		if (sources.empty()) {
			set_failure(ctx, "No synthesizable RTL source files found");
			return "Error: No synthesizable SystemVerilog/Verilog source files found in workspace.";
		}

		loaded = analyzer.synthesize_and_load(args_.requested_module, sources, target_mode, err);
		if (!loaded) {
			set_failure(ctx, err);
			return std::format("Synthesis/analysis failed for module '{}': {}", args_.requested_module, err);
		}
	}

	std::string md_tables = analyzer.generate_timing_markdown(target_mode);

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

	auto is_carry_cell = [](std::string_view cell_type) -> bool {
		return cell_type.find("CARRY") != std::string_view::npos ||
		       cell_type.find("CCU2") != std::string_view::npos;
	};

	auto format_depth_breakdown = [&](size_t depth, std::span<const verilog::path_hop> hops) -> std::string {
		if (target_mode == "Generic Gates") {
			return (depth == 1) ? "1 gate" : std::format("{} gates", depth);
		}
		size_t carry_hops = 0;
		size_t lut_hops = 0;
		for (const auto &h : hops) {
			if (is_carry_cell(h.cell_type)) {
				carry_hops++;
			} else {
				lut_hops++;
			}
		}
		if (carry_hops > 0 && lut_hops > 0) {
			return std::format("{} ({} {} + {} carry)", depth, lut_hops, (lut_hops == 1) ? "LUT" : "LUTs", carry_hops);
		}
		if (carry_hops > 0) {
			return std::format("{} ({} carry)", depth, carry_hops);
		}
		return (depth == 1) ? "1 LUT" : std::format("{} LUTs", depth);
	};

	auto format_hops_chain = [&](std::span<const verilog::path_hop> hops) -> std::string {
		struct collapsed_stage {
			std::string src_location;
			size_t count{0};
			size_t lut_count{0};
			size_t carry_count{0};
			std::vector<std::string> input_hints;
			std::string out_net;
		};
		std::vector<collapsed_stage> stages;

		for (const auto &hop : hops) {
			std::string hop_loc = verilog::nextpnr_timing_analyzer::clean_display_location(hop.src_location);
			bool is_carry = is_carry_cell(hop.cell_type);
			bool can_merge = false;
			if (!stages.empty()) {
				if (!hop_loc.empty() && stages.back().src_location == hop_loc) {
					can_merge = true;
				} else if (args_.collapse && hop_loc.empty() && stages.back().src_location.empty()) {
					can_merge = true;
				}
			}

			if (can_merge) {
				stages.back().count++;
				if (is_carry) {
					stages.back().carry_count++;
				} else {
					stages.back().lut_count++;
				}
				if (!hop.input_hints.empty()) {
					stages.back().input_hints.push_back(hop.input_hints);
				}
				if (!hop.net_name.empty()) {
					stages.back().out_net = hop.net_name;
				}
			} else {
				collapsed_stage st;
				st.src_location = hop_loc;
				st.count = 1;
				if (is_carry) {
					st.carry_count = 1;
				} else {
					st.lut_count = 1;
				}
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
			if (target_mode == "Generic Gates") {
				stage_units = (st.count == 1) ? "1 gate" : std::format("{} gates", st.count);
			} else if (st.carry_count > 0 && st.lut_count > 0) {
				stage_units = std::format("{} {} + {} carry", st.lut_count, (st.lut_count == 1) ? "LUT" : "LUTs", st.carry_count);
			} else if (st.carry_count > 0) {
				stage_units = std::format("{} carry", st.carry_count);
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
							std::string norm_sub = verilog::nextpnr_timing_analyzer::normalize_net_name(sub);
							if (!norm_sub.empty() && norm_sub.find('$') == std::string::npos &&
							    !norm_sub.starts_with("wire_") && !norm_sub.starts_with("new_n") &&
							    !norm_sub.starts_with("SB_") && !norm_sub.starts_with("_SB_")) {
								token_hints.push_back(norm_sub);
							}
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
			std::string norm_out = verilog::nextpnr_timing_analyzer::normalize_net_name(st.out_net);
			if (!norm_out.empty() && norm_out.find('$') == std::string::npos &&
			    !norm_out.starts_with("new_n") && !norm_out.starts_with("wire_")) {
				out_str = std::format(" -> `{}`", norm_out);
			}

			chain_md += std::format("  {}. {} ({}{}){}\n", s_idx + 1, loc_str, stage_units, hint_str, out_str);
		}
		return chain_md;
	};

	std::string paths_md;

	if (args_.group_by_sink) {
		auto bottlenecks = analyzer.get_worst_bottlenecks(std::nullopt, args_.top_k);
		if (!bottlenecks.empty()) {
			paths_md += "\n## Worst Combinational Paths (Grouped by Bottleneck Sink)\n\n";

			const auto &top = bottlenecks.front();
			std::string top_cats = format_cats(top.categories);
			std::string top_depth_str = format_depth_breakdown(top.depth, top.hops);
			if (top.sources.size() > 1) {
				paths_md += std::format("**Critical Path Summary**: {}, {} (converging on `{}` from {} source signals)\n\n",
				                        top_cats, top_depth_str,
				                        verilog::nextpnr_timing_analyzer::normalize_net_name(top.endpoint),
				                        top.sources.size());
			} else if (top.sources.size() == 1) {
				paths_md += std::format("**Critical Path Summary**: {}, {} (from `{}` to `{}`)\n\n",
				                        top_cats, top_depth_str,
				                        verilog::nextpnr_timing_analyzer::normalize_net_name(top.sources.front().name),
				                        verilog::nextpnr_timing_analyzer::normalize_net_name(top.endpoint));
			}

			for (size_t i = 0; i < bottlenecks.size(); ++i) {
				const auto &b = bottlenecks[i];
				std::string cat_str = format_cats(b.categories);
				std::string depth_str = format_depth_breakdown(b.depth, b.hops);
				std::string norm_b_endpoint = verilog::nextpnr_timing_analyzer::normalize_net_name(b.endpoint);

				if (b.sources.size() > 1) {
					paths_md += std::format("### Path #{} [{}] Depth: {} — Converging on `{}`\n",
					                        i + 1, cat_str, depth_str, norm_b_endpoint);
					std::string src_list = verilog::netlist_graph_analyzer::format_source_ranges(b.sources);
					paths_md += std::format("- **Sources** ({} source bits, all converge here): {}\n",
					                        b.sources.size(), src_list);
				} else if (b.sources.size() == 1) {
					paths_md += std::format("### Path #{} [{}] Depth: {}\n", i + 1, cat_str, depth_str);
					std::string start_src = verilog::nextpnr_timing_analyzer::clean_display_location(b.sources.front().src_location);
					paths_md += std::format("- **Start**: `{}` {}\n",
					                        verilog::nextpnr_timing_analyzer::normalize_net_name(b.sources.front().name),
					                        start_src.empty() ? "" : std::format("(`{}`)", start_src));
				} else {
					paths_md += std::format("### Path #{} [{}] Depth: {}\n", i + 1, cat_str, depth_str);
				}

				paths_md += format_hops_chain(b.hops);

				std::string end_src = verilog::nextpnr_timing_analyzer::clean_display_location(b.end_src);
				paths_md += std::format("- **End**: `{}` {}\n", norm_b_endpoint,
				                        end_src.empty() ? "" : std::format("(`{}`)", end_src));
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
			std::string top_depth_str = format_depth_breakdown(top.depth, top.hops);
			paths_md += std::format("**Critical Path Summary**: {}, {} (from `{}` to `{}`)\n\n",
			                        top_cat_str, top_depth_str,
			                        verilog::nextpnr_timing_analyzer::normalize_net_name(top.startpoint),
			                        verilog::nextpnr_timing_analyzer::normalize_net_name(top.endpoint));

			for (size_t i = 0; i < worst_paths.size(); ++i) {
				const auto &p = worst_paths[i];
				std::string cat_str = format_cat(p.category);
				std::string depth_str = format_depth_breakdown(p.depth, p.hops);

				std::string norm_start = verilog::nextpnr_timing_analyzer::normalize_net_name(p.startpoint);
				std::string norm_end = verilog::nextpnr_timing_analyzer::normalize_net_name(p.endpoint);
				std::string start_src = verilog::nextpnr_timing_analyzer::clean_display_location(p.start_src);
				std::string end_src = verilog::nextpnr_timing_analyzer::clean_display_location(p.end_src);

				paths_md += std::format("### Path #{} [{}] Depth: {}\n", i + 1, cat_str, depth_str);
				paths_md += std::format("- **Start**: `{}` {}\n", norm_start,
				                        start_src.empty() ? "" : std::format("(`{}`)", start_src));

				paths_md += format_hops_chain(p.hops);

				paths_md += std::format("- **End**: `{}` {}\n", norm_end,
				                        end_src.empty() ? "" : std::format("(`{}`)", end_src));
				if (!p.note.empty()) {
					paths_md += std::format("  > Note: {}\n", p.note);
				}
				paths_md += "\n";
			}
		}
	}

	set_success(ctx);
	return md_tables + paths_md;
}

} // namespace tools
