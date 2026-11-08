#include "plugins/verilog/verilog_pnr_timing_tool.h"
#include "plugins/verilog/nextpnr_timing_analyzer.h"
#include "plugins/verilog/netlist_graph_analyzer.h"
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
		return analyzer.generate_timing_markdown(args_.reg_to_reg_only, args_.group_by_sink, args_.top_k);
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
	std::vector<std::string> sources;
	fs::path scan_root;
	if (!args_.safe_file.empty()) {
		scan_root = fs::path(args_.safe_file).parent_path();
		if (scan_root.has_parent_path() && scan_root.filename() != "sv") {
			scan_root = scan_root.parent_path();
		}
	} else {
		scan_root = fs::path(project_root);
	}

	try {
		std::error_code ec;
		for (const auto &entry : fs::recursive_directory_iterator(scan_root, fs::directory_options::skip_permission_denied, ec)) {
			if (entry.is_regular_file(ec) && verilog::netlist_graph_analyzer::is_synthesis_source_file(entry.path())) {
				// Don't include testbenches
				std::string filename = entry.path().filename().string();
				if (!filename.starts_with("tb_") && filename.find("_tb.") == std::string::npos) {
					sources.push_back(entry.path().string());
				}
			}
		}
	} catch (...) {
	}

	if (sources.empty() && !args_.safe_file.empty()) {
		sources.push_back(args_.safe_file);
	}

	if (sources.empty()) {
		set_failure(ctx, "No synthesizable RTL source files found");
		return "Error: No synthesizable SystemVerilog/Verilog source files found in workspace.";
	}

	if (!analyzer.synthesize_and_route(cfg, sources, err)) {
		set_failure(ctx, err);
		return std::format("On-demand synthesis and place-and-route failed: {}", err);
	}

	set_success(ctx);
	return analyzer.generate_timing_markdown(args_.reg_to_reg_only, args_.group_by_sink, args_.top_k);
}

} // namespace tools
