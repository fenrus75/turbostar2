#include "vcd_file_info_validator.h"
#include "agentlib/json_utils.h"
#include "agentlib/tool_registry.h"
#include <format>

namespace tools
{

std::string vcd_file_info_validator::get_description() const
{
	return "Inspects an IEEE 1364/1800 Value Change Dump (VCD) simulation file. "
	       "Returns timescale, time duration, detected clock signal, period, total clock ticks, "
	       "and a markdown table of signals filtered by hierarchical scope or name pattern.";
}

nlohmann::json vcd_file_info_validator::get_parameters_schema() const
{
	return {
		{"type", "object"},
		{"properties", {
			{"path", {
				{"type", "string"},
				{"description", "Path to the VCD file (e.g. 'sim/dump.vcd' or 'tmp://trace.vcd')."}
			}},
			{"clock_signal", {
				{"type", "string"},
				{"description", "Optional signal name to analyze as the reference clock. If omitted, the tool automatically detects the primary clock."}
			}},
			{"scope", {
				{"type", "string"},
				{"description", "Optional hierarchical module scope to filter signals (e.g. 'tb_main.uut.cpu8008')."}
			}},
			{"filter", {
				{"type", "string"},
				{"description", "Optional wildcard pattern to filter signal names (e.g. '*addr*' or '*data*')."}
			}},
			{"max_signals", {
				{"type", "integer"},
				{"description", "Maximum number of signals to display in the output table (default: 100)."}
			}}
		}},
		{"required", {"path"}}
	};
}

bool vcd_file_info_validator::validate_args_impl(const nlohmann::json &raw_json,
						 const agentlib::tool_context &ctx,
						 std::string &out_error) const
{
	if (!raw_json.contains("path") || !raw_json["path"].is_string()) {
		out_error = "Missing required string argument 'path'";
		return false;
	}

	args_ = vcd_file_info_args{};
	args_.requested_path = raw_json["path"].get<std::string>();

	if (args_.requested_path.empty()) {
		out_error = "Argument 'path' cannot be empty";
		return false;
	}

	std::string canonical_path;
	std::string sec_err;
	if (!ctx.fs_security.validate_access(args_.requested_path, agentlib::access_type::read, canonical_path, sec_err)) {
		out_error = std::format("Security validation failed for '{}': {}", args_.requested_path, sec_err);
		return false;
	}
	args_.safe_path = canonical_path;

	if (raw_json.contains("clock_signal") && raw_json["clock_signal"].is_string()) {
		args_.clock_signal = raw_json["clock_signal"].get<std::string>();
	}

	if (raw_json.contains("scope") && raw_json["scope"].is_string()) {
		args_.scope = raw_json["scope"].get<std::string>();
	}

	if (raw_json.contains("filter") && raw_json["filter"].is_string()) {
		args_.filter = raw_json["filter"].get<std::string>();
	}

	if (raw_json.contains("max_signals")) {
		std::string num_err;
		size_t max_s = 100;
		if (json_utils::get_number(raw_json, "max_signals", max_s, static_cast<size_t>(100), num_err) && max_s > 0) {
			args_.max_signals = max_s;
		}
	}

	return true;
}

std::unique_ptr<agentlib::llm_tool> vcd_file_info_validator::create_tool_impl(const nlohmann::json &/*raw_json*/) const
{
	return std::make_unique<vcd_file_info_tool>(args_);
}

} // namespace tools

extern "C" {

void register_vcd_file_info(void)
{
	agentlib::tool_registry::get_instance().register_validator(
		[]() { return std::make_unique<tools::vcd_file_info_validator>(); }
	);
}

void unregister_vcd_file_info(void)
{
	agentlib::tool_registry::get_instance().unregister_validator("vcd_file_info");
}

}
