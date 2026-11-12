#include "plugins/verilog/verilog_pnr_timing_tool.h"
#include "plugins/verilog/nextpnr_timing_analyzer.h"
#include "plugins/verilog/netlist_graph_analyzer.h"
#include "plugins/verilog/verilog_module_scanner.h"
#include "plugins/verilog/fpga_project_settings.h"
#include "fs_utils.h"

#include <filesystem>
#include <format>
#include <vector>

namespace tools
{

namespace fs = std::filesystem;

verilog_pnr_timing_tool::verilog_pnr_timing_tool(verilog_pnr_timing_args args)
	: agentlib::llm_tool_action("Analyzing physical place-and-route timing"),
	  args_(std::move(args))
{
}

bool verilog_pnr_timing_tool::validate_runtime(const agentlib::tool_context &/*ctx*/, std::string &/*out_error*/) const
{
	return true;
}

std::string verilog_pnr_timing_tool::execute(agentlib::tool_context &ctx)
{
	verilog::nextpnr_timing_analyzer analyzer;
	std::string err;

	// Mode 1: Pre-existing timing report provided
	if (!args_.safe_timing_report.empty()) {
		if (!analyzer.load_from_files(args_.safe_timing_report, args_.safe_netlist, err)) {
			set_failure(ctx, err);
			return std::format("Error loading timing report '{}': {}", args_.safe_timing_report, err);
		}
		set_success(ctx);
		return analyzer.generate_timing_markdown(args_.reg_to_reg_only, args_.group_by_sink, args_.top_k, args_.collapse);
	}

	// Mode 2: On-demand place-and-route
	std::string project_root = ctx.fs_security.get_working_directory().string();
	if (project_root.empty()) {
		project_root = ".";
	}

	verilog::fpga_target_config cfg;
	std::string load_err;
	verilog::fpga_project_settings::load_from_project(project_root, cfg, load_err);

	auto sys_tools = verilog::fpga_project_settings::detect_system_tools();
	auto pcfs = verilog::fpga_project_settings::scan_pcf_files(project_root);

	// If target is unset but only 1 nextpnr tool exists on system, auto-default
	if (cfg.target.empty() && sys_tools.available_targets.size() == 1) {
		cfg.target = sys_tools.available_targets.front();
	}

	std::string configured_top = cfg.top_module;
	if (!args_.requested_module.empty() && cfg.top_module.empty()) {
		cfg.top_module = args_.requested_module;
	}

	std::vector<std::string> missing;
	if (!verilog::fpga_project_settings::is_complete(cfg, missing)) {
		set_success(ctx);
		return verilog::fpga_project_settings::format_guidance(project_root, cfg, sys_tools, pcfs);
	}

	// If PCF not configured, but single PCF exists in repo, use it
	if (cfg.pcf_file.empty() && pcfs.size() == 1) {
		cfg.pcf_file = pcfs.front();
	}

	// Gather synthesizable RTL files
	fs::path scan_root;
	if (!args_.safe_file.empty()) {
		scan_root = fs::path(args_.safe_file).parent_path();
		auto is_rtl_root_dir = [&](const fs::path &d) {
			std::string fname = d.filename().string();
			std::error_code ec;
			return fname == "sv" || fname == "src" || fname == "rtl" ||
			       (!cfg.sv_path.empty() && fname == cfg.sv_path) ||
			       fs::equivalent(d, fs::path(project_root), ec);
		};
		if (scan_root.has_parent_path() && !is_rtl_root_dir(scan_root)) {
			scan_root = scan_root.parent_path();
		}
	} else if (!cfg.sv_path.empty()) {
		scan_root = fs::path(project_root) / cfg.sv_path;
	} else {
		scan_root = fs::path(project_root);
	}

	std::vector<std::string> sources = verilog::verilog_module_scanner::find_synthesizable_files(scan_root);
	if (sources.empty() && !args_.safe_file.empty()) {
		sources.push_back(args_.safe_file);
	}

	if (sources.empty()) {
		set_failure(ctx, "No synthesizable RTL source files found");
		return "Error: No synthesizable SystemVerilog/Verilog source files found in workspace.";
	}

	std::string submodule_banner;
	if (!args_.requested_module.empty()) {
		std::string resolved_mod = verilog::verilog_module_scanner::resolve_module_name(
			args_.requested_module, args_.safe_file, sources);
		if (!resolved_mod.empty()) {
			cfg.top_module = resolved_mod;
			if (!configured_top.empty() && resolved_mod != configured_top) {
				// Out-of-context submodule PnR: omit top-level PCF pin constraints so nextpnr places submodule I/Os freely
				cfg.pcf_file.clear();
				submodule_banner = std::format(
					"> [!NOTE] **Out-of-Context Submodule PnR (`{}`)**: Analyzed submodule `{}` with unconstrained I/O placement (configured project `top_module` is `{}`).\n\n",
					resolved_mod, resolved_mod, configured_top);
			}
		}
	}

	if (!analyzer.synthesize_and_route(cfg, sources, err)) {
		set_failure(ctx, err);
		return std::format("On-demand synthesis and place-and-route failed: {}", err);
	}

	set_success(ctx);
	return submodule_banner + analyzer.generate_timing_markdown(args_.reg_to_reg_only, args_.group_by_sink, args_.top_k, args_.collapse);
}

} // namespace tools
