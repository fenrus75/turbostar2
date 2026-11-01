#include "vcd_signal_to_code_validator.h"
#include "agentlib/tool_registry.h"
#include <format>

namespace tools
{

std::string vcd_signal_to_code_validator::get_description() const
{
	return "Traces a simulation signal across the design hierarchy to source code file and line numbers using a Yosys JSON netlist. "
	       "Identifies signal declarations across module boundaries, the driving cell/port, and consuming sink logic in a token-efficient summary.";
}

nlohmann::json vcd_signal_to_code_validator::get_parameters_schema() const
{
	return {
		{"type", "object"},
		{"properties", {
			{"signal", {
				{"type", "string"},
				{"description", "Simulation signal name (e.g. 'tb_main.uut.cpu8008.A_val' or 'cpu8008.A_val')."}
			}},
			{"netlist", {
				{"type", "string"},
				{"description", "Optional path to a Yosys JSON netlist (generated via 'yosys ... prep -flatten; write_json netlist.json'). If omitted, auto-discovers netlist.json in project."}
			}}
		}},
		{"required", {"signal"}}
	};
}

bool vcd_signal_to_code_validator::validate_args_impl(const nlohmann::json &raw_json,
						      const agentlib::tool_context &ctx,
						      std::string &out_error) const
{
	if (!raw_json.contains("signal") || !raw_json["signal"].is_string()) {
		out_error = "Missing required string argument 'signal'";
		return false;
	}

	args_ = vcd_signal_to_code_args{};
	args_.signal = raw_json["signal"].get<std::string>();
	if (args_.signal.empty()) {
		out_error = "Argument 'signal' cannot be empty";
		return false;
	}

	if (raw_json.contains("netlist") && raw_json["netlist"].is_string() && !raw_json["netlist"].get<std::string>().empty()) {
		args_.requested_netlist = raw_json["netlist"].get<std::string>();
		std::string untrusted_path = args_.requested_netlist;
		std::string safe_path;
		std::string sec_err;
		if (!ctx.fs_security.validate_access(untrusted_path, agentlib::access_type::read, safe_path, sec_err)) {
			out_error = std::format("Access denied to netlist path '{}': {}", untrusted_path, sec_err);
			return false;
		}
		args_.safe_netlist = safe_path;
	}

	return true;
}

std::unique_ptr<agentlib::llm_tool> vcd_signal_to_code_validator::create_tool_impl(const nlohmann::json &/*raw_json*/) const
{
	return std::make_unique<vcd_signal_to_code_tool>(args_);
}

} // namespace tools

extern "C"
{
void register_vcd_signal_to_code(void)
{
	agentlib::tool_registry::get_instance().register_validator(
		[]() { return std::make_unique<tools::vcd_signal_to_code_validator>(); }
	);
}

void unregister_vcd_signal_to_code(void)
{
	agentlib::tool_registry::get_instance().unregister_validator("vcd_signal_to_code");
}
}
