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

		if (target_mode == "Generic LUT4" && cfg.target == "ice40") {
			target_mode = "iCE40";
		}

		std::vector<std::string> sources;
		if (!args_.safe_file.empty()) {
			std::filesystem::path fp(args_.safe_file);
			// Gather synthesizable sibling RTL files from the enclosing directory tree
			std::filesystem::path scan_root = fp.parent_path();
			auto is_rtl_root_dir = [&](const std::filesystem::path &d) {
				std::string fname = d.filename().string();
				std::error_code ec;
				return fname == "sv" || fname == "src" || fname == "rtl" ||
				       (!cfg.sv_path.empty() && fname == cfg.sv_path) ||
				       std::filesystem::equivalent(d, std::filesystem::path(project_root), ec);
			};
			if (scan_root.has_parent_path() && !is_rtl_root_dir(scan_root)) {
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

		std::string resolved_module = verilog::verilog_module_scanner::resolve_module_name(
			args_.requested_module, args_.safe_file, sources);
		if (resolved_module.empty()) {
			if (!cfg.top_module.empty()) {
				resolved_module = cfg.top_module;
			} else {
				auto discovered = verilog::verilog_module_scanner::discover_modules_in_files(sources);
				if (discovered.size() == 1) {
					resolved_module = discovered.front().module_name;
				} else {
					set_failure(ctx, "No module name specified and no top_module configured in project settings");
					return "Error: No module name specified and no top_module configured in turbostar.json. "
					       "Please pass 'module' parameter or configure 'top_module' via verilog_project_settings.";
				}
			}
		}
		args_.requested_module = resolved_module;

		loaded = analyzer.synthesize_and_load(args_.requested_module, sources, target_mode, err);
		if (!loaded) {
			auto discovered = verilog::verilog_module_scanner::discover_modules_in_files(sources);
			if (!discovered.empty()) {
				std::string mod_list;
				for (size_t i = 0; i < discovered.size(); ++i) {
					if (i > 0) mod_list += ", ";
					mod_list += std::format("`{}` ({})", discovered[i].module_name,
					                        std::filesystem::path(discovered[i].source_file).filename().string());
				}
				err += std::format("\nAvailable RTL modules: {}", mod_list);
			}
			set_failure(ctx, err);
			return std::format("Synthesis/analysis failed for module '{}': {}", args_.requested_module, err);
		}
	}

	std::string base_dir = ctx.fs_security.get_working_directory().string();
	if (!args_.safe_file.empty()) {
		std::filesystem::path sdir = std::filesystem::path(args_.safe_file).parent_path();
		if ((sdir.filename() == "sv" || sdir.filename() == "src" || sdir.filename() == "rtl") && sdir.has_parent_path()) {
			sdir = sdir.parent_path();
		}
		base_dir = sdir.string();
	} else if (!args_.safe_netlist.empty()) {
		std::filesystem::path ndir = std::filesystem::path(args_.safe_netlist).parent_path();
		if ((ndir.filename() == "synth" || ndir.filename() == "build") && ndir.has_parent_path()) {
			ndir = ndir.parent_path();
		}
		base_dir = ndir.string();
	}

	set_success(ctx);
	return analyzer.generate_logic_depth_report_markdown(
		target_mode, args_.top_k, args_.group_by_sink, args_.collapse, args_.show_code, base_dir);
}

} // namespace tools
