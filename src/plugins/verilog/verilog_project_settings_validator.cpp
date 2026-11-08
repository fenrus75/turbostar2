#include "plugins/verilog/verilog_project_settings_validator.h"
#include "agentlib/tool_registry.h"
#include "fs_utils.h"

namespace tools
{

std::string verilog_project_settings_validator::get_description() const
{
	return "Inspects or updates FPGA hardware project configuration (persisted in turbostar.json or project.json). "
	       "When called without parameters or with action='get', reports current settings, detected EDA tools, "
	       "discovered PCF pin constraint files, and missing parameter guidance. "
	       "When called with configuration parameters or action='set', updates and saves the project settings.";
}

nlohmann::json verilog_project_settings_validator::get_parameters_schema() const
{
	return nlohmann::json{
		{"type", "object"},
		{"properties", {
			{"action", {
				{"type", "string"},
				{"enum", {"get", "set"}},
				{"description", "Optional action: 'get' to inspect current configuration and environment (default), or 'set' to update."}
			}},
			{"target", {
				{"type", "string"},
				{"description", "Target FPGA architecture (e.g. 'ice40', 'generic', 'ecp5')."}
			}},
			{"device", {
				{"type", "string"},
				{"description", "FPGA device part or flag (e.g. 'hx8k', 'lp8k', '--hx8k')."}
			}},
			{"package", {
				{"type", "string"},
				{"description", "FPGA package type or flag (e.g. 'cb132', 'sg48', '--package cb132')."}
			}},
			{"top_module", {
				{"type", "string"},
				{"description", "Synthesizable top-level SystemVerilog module name (e.g. 'main' or 'top')."}
			}},
			{"freq_mhz", {
				{"type", "number"},
				{"description", "Target clock frequency in MHz (e.g. 60.0)."}
			}},
			{"pcf_file", {
				{"type", "string"},
				{"description", "Path to physical pin constraint file (*.pcf), e.g. 'pcf/cu.pcf'."}
			}},
			{"sv_path", {
				{"type", "string"},
				{"description", "Root directory for synthesizable SystemVerilog/Verilog sources (e.g. 'sv', 'rtl', or 'src')."}
			}},
			{"default_sv_path", {
				{"type", "string"},
				{"description", "Alias for sv_path."}
			}},
			{"yosys_synth_args", {
				{"type", "string"},
				{"description", "Optional extra synthesis arguments for Yosys (e.g. '-abc9')."}
			}}
		}},
		{"required", nlohmann::json::array()}
	};
}

bool verilog_project_settings_validator::validate_args_impl(const nlohmann::json &untrusted_args,
                                                           const agentlib::tool_context &/*ctx*/,
                                                           std::string &out_error) const
{
	args_ = verilog_project_settings_args{};

	if (untrusted_args.contains("action") && untrusted_args["action"].is_string()) {
		std::string act = untrusted_args["action"].get<std::string>();
		if (act != "get" && act != "set") {
			out_error = "Parameter 'action' must be 'get' or 'set'";
			return false;
		}
		args_.action = act;
	}

	if (untrusted_args.contains("target") && untrusted_args["target"].is_string()) {
		args_.target = untrusted_args["target"].get<std::string>();
	}
	if (untrusted_args.contains("device") && untrusted_args["device"].is_string()) {
		args_.device = untrusted_args["device"].get<std::string>();
	}
	if (untrusted_args.contains("package") && untrusted_args["package"].is_string()) {
		args_.package = untrusted_args["package"].get<std::string>();
	}
	if (untrusted_args.contains("top_module") && untrusted_args["top_module"].is_string()) {
		args_.top_module = untrusted_args["top_module"].get<std::string>();
	}
	if (untrusted_args.contains("freq_mhz") && untrusted_args["freq_mhz"].is_number()) {
		args_.freq_mhz = untrusted_args["freq_mhz"].get<double>();
	}
	if (untrusted_args.contains("pcf_file") && untrusted_args["pcf_file"].is_string()) {
		args_.pcf_file = untrusted_args["pcf_file"].get<std::string>();
	}
	if (untrusted_args.contains("sv_path") && untrusted_args["sv_path"].is_string()) {
		args_.sv_path = untrusted_args["sv_path"].get<std::string>();
	} else if (untrusted_args.contains("default_sv_path") && untrusted_args["default_sv_path"].is_string()) {
		args_.sv_path = untrusted_args["default_sv_path"].get<std::string>();
	}
	if (untrusted_args.contains("yosys_synth_args") && untrusted_args["yosys_synth_args"].is_string()) {
		args_.yosys_synth_args = untrusted_args["yosys_synth_args"].get<std::string>();
	}

	return true;
}

std::unique_ptr<agentlib::llm_tool> verilog_project_settings_validator::create_tool_impl(const nlohmann::json &/*untrusted_args*/) const
{
	return std::make_unique<verilog_project_settings_tool>(args_);
}

} // namespace tools

extern "C" {

void register_verilog_project_settings(void)
{
	agentlib::tool_registry::get_instance().register_validator(
		[]() { return std::make_unique<tools::verilog_project_settings_validator>(); }
	);
}

void unregister_verilog_project_settings(void)
{
	agentlib::tool_registry::get_instance().unregister_validator("verilog_project_settings");
}

}
