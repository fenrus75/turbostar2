#include "plugins/verilog/verilog_logic_depth_tool.h"
#include "plugins/verilog/netlist_graph_analyzer.h"
#include "fs_utils.h"

#include <filesystem>
#include <fstream>
#include <format>

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
			sources.push_back(args_.safe_file);
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

	// Worst paths section
	auto worst_paths = analyzer.get_worst_paths(std::nullopt, args_.top_k);
	std::string paths_md;
	if (!worst_paths.empty()) {
		paths_md += "\n## Worst Combinational Paths\n\n";
		for (size_t i = 0; i < worst_paths.size(); ++i) {
			const auto &p = worst_paths[i];
			std::string cat_str;
			switch (p.category) {
			case verilog::path_category::reg_to_reg:
				cat_str = "Reg -> Reg";
				break;
			case verilog::path_category::in_to_reg:
				cat_str = "In -> Reg";
				break;
			case verilog::path_category::reg_to_out:
				cat_str = "Reg -> Out";
				break;
			case verilog::path_category::in_to_out:
				cat_str = "In -> Out";
				break;
			}

			paths_md += std::format("### Path #{} [{}] Depth: {} {}\n", i + 1, cat_str, p.depth,
			                        (args_.target_mode == "Generic Gates") ? "gates" : "LUTs");
			paths_md += std::format("- **Start**: `{}` {}\n", p.startpoint,
			                        p.start_src.empty() ? "" : std::format("({})", p.start_src));

			for (size_t h_idx = 0; h_idx < p.hops.size(); ++h_idx) {
				const auto &hop = p.hops[h_idx];
				paths_md += std::format("  {}. `{}` `{}` {} -> net `{}`\n",
				                        h_idx + 1, hop.cell_type, hop.cell_name,
				                        hop.src_location.empty() ? "" : std::format("({})", hop.src_location),
				                        hop.net_name);
			}

			paths_md += std::format("- **End**: `{}` {}\n\n", p.endpoint,
			                        p.end_src.empty() ? "" : std::format("({})", p.end_src));
		}
	}

	set_success(ctx);
	return md_tables + sidecar_note + paths_md;
}

} // namespace tools
