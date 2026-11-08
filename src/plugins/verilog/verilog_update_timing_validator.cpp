#include "plugins/verilog/verilog_update_timing_validator.h"
#include "agentlib/tool_registry.h"

namespace tools
{

std::string verilog_update_timing_validator::get_description() const
{
	return "Scans Verilog/SystemVerilog modules across the project or specified directory, computes logic depth metrics, and updates all <module>.timing.md sidecar files in one batch.";
}

nlohmann::json verilog_update_timing_validator::get_parameters_schema() const
{
	return nlohmann::json{
		{"type", "object"},
		{"properties", {
			{"path", {
				{"type", "string"},
				{"description", "Optional. Directory or file path to scan for Verilog/SystemVerilog modules. Defaults to project directory ('.')."}
			}},
			{"netlist", {
				{"type", "string"},
				{"description", "Optional. Path to a pre-generated Yosys JSON netlist. If omitted, modules are synthesized on-the-fly."}
			}},
			{"target", {
				{"type", "string"},
				{"enum", {"Generic LUT4", "Generic LUT6", "Generic Gates", "iCE40"}},
				{"description", "Optional. Synthesis/timing target mode. Defaults to 'Generic LUT4'."}
			}}
		}}
	};
}

bool verilog_update_timing_validator::validate_args_impl(const nlohmann::json &raw_json,
                                                         const agentlib::tool_context &ctx,
                                                         std::string &out_error) const
{
	args_ = verilog_update_timing_args{};

	if (raw_json.contains("path") && raw_json["path"].is_string()) {
		args_.requested_path = raw_json["path"].get<std::string>();
	}

	std::string sec_err;
	if (!ctx.fs_security.validate_access(args_.requested_path, agentlib::access_type::read,
	                                    args_.safe_path, sec_err)) {
		out_error = sec_err;
		return false;
	}

	if (raw_json.contains("netlist") && raw_json["netlist"].is_string()) {
		args_.requested_netlist = raw_json["netlist"].get<std::string>();
		if (!ctx.fs_security.validate_access(args_.requested_netlist, agentlib::access_type::read,
		                                    args_.safe_netlist, sec_err)) {
			out_error = sec_err;
			return false;
		}
	}

	if (raw_json.contains("target") && raw_json["target"].is_string()) {
		args_.target_mode = raw_json["target"].get<std::string>();
	}

	return true;
}

std::unique_ptr<agentlib::llm_tool> verilog_update_timing_validator::create_tool_impl(const nlohmann::json &/*raw_json*/) const
{
	return std::make_unique<verilog_update_timing_tool>(args_);
}

extern "C" {

void register_verilog_update_timing(void)
{
	agentlib::tool_registry::get_instance().register_validator(
		[]() { return std::make_unique<tools::verilog_update_timing_validator>(); }
	);
}

void unregister_verilog_update_timing(void)
{
	agentlib::tool_registry::get_instance().unregister_validator("verilog_update_timing");
}

}

} // namespace tools
