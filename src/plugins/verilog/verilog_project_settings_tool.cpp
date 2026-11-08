#include "plugins/verilog/verilog_project_settings_tool.h"
#include "plugins/verilog/fpga_project_settings.h"
#include "fs_utils.h"

#include <format>
#include <vector>

namespace tools
{

verilog_project_settings_tool::verilog_project_settings_tool(verilog_project_settings_args args)
	: agentlib::llm_tool_action("Managing FPGA project settings"),
	  args_(std::move(args))
{
}

bool verilog_project_settings_tool::validate_runtime(const agentlib::tool_context &/*ctx*/, std::string &/*out_error*/) const
{
	return true;
}

std::string verilog_project_settings_tool::execute(agentlib::tool_context &ctx)
{
	std::string project_root = ctx.fs_security.get_working_directory().string();
	if (project_root.empty()) {
		project_root = ".";
	}

	verilog::fpga_target_config cfg;
	std::string load_err;
	verilog::fpga_project_settings::load_from_project(project_root, cfg, load_err);

	auto sys_tools = verilog::fpga_project_settings::detect_system_tools();
	auto pcfs = verilog::fpga_project_settings::scan_pcf_files(project_root);

	bool is_set_action = (args_.action == "set") ||
	                     !args_.target.empty() ||
	                     !args_.device.empty() ||
	                     !args_.package.empty() ||
	                     !args_.top_module.empty() ||
	                     (args_.freq_mhz > 0.0) ||
	                     !args_.pcf_file.empty() ||
	                     !args_.sv_path.empty() ||
	                     !args_.yosys_synth_args.empty();

	if (is_set_action) {
		if (!args_.target.empty()) cfg.target = args_.target;
		if (!args_.device.empty()) cfg.device = args_.device;
		if (!args_.package.empty()) cfg.package = args_.package;
		if (!args_.top_module.empty()) cfg.top_module = args_.top_module;
		if (args_.freq_mhz > 0.0) cfg.freq_mhz = args_.freq_mhz;
		if (!args_.pcf_file.empty()) cfg.pcf_file = args_.pcf_file;
		if (!args_.sv_path.empty()) cfg.sv_path = args_.sv_path;
		if (!args_.yosys_synth_args.empty()) cfg.yosys_synth_args = args_.yosys_synth_args;

		// Default PCF if not set and exactly one is present
		if (cfg.pcf_file.empty() && pcfs.size() == 1) {
			cfg.pcf_file = pcfs.front();
		}

		std::string path_used;
		std::string save_err;
		if (!verilog::fpga_project_settings::save_to_project(project_root, cfg, path_used, save_err)) {
			set_failure(ctx, save_err);
			return std::format("Error saving project settings: {}", save_err);
		}

		cfg.active_config_file = path_used;

		set_success(ctx);
		std::string out = std::format("Successfully saved FPGA project configuration to `{}`.\n\n", path_used);
		out += verilog::fpga_project_settings::format_summary(cfg, sys_tools, pcfs);
		return out;
	}

	// GET mode
	set_success(ctx);
	std::string summary = verilog::fpga_project_settings::format_summary(cfg, sys_tools, pcfs);

	std::vector<std::string> missing;
	if (!verilog::fpga_project_settings::is_complete(cfg, missing)) {
		summary += "\n---\n\n";
		summary += verilog::fpga_project_settings::format_guidance(project_root, cfg, sys_tools, pcfs);
	}

	return summary;
}

} // namespace tools
