#include "vcd_file_info_validator.h"
#include "agentlib/json_utils.h"
#include "agentlib/tool_registry.h"
#include "plugins/verilog/verilog_module_scanner.h"
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
				{"description", "Path to the VCD file (e.g. 'dump.vcd' or 'tmp://trace.vcd'). If omitted, automatically resolved from $dumpfile(...) in the project's testbench."}
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
			}},
			{"stats", {
				{"type", "boolean"},
				{"description", "If true, expands the signal table with 'First Toggle' tick and 'Toggle Rate' columns."}
			}},
			{"start_tick", {
				{"type", "integer"},
				{"description", "Optional starting clock tick for toggle statistics windowing (inclusive). Automatically enables stats."}
			}},
			{"end_tick", {
				{"type", "integer"},
				{"description", "Optional ending clock tick for toggle statistics windowing (inclusive). Automatically enables stats."}
			}}
		}},
	};
}

bool vcd_file_info_validator::validate_args_impl(const nlohmann::json &raw_json,
						 const agentlib::tool_context &ctx,
						 std::string &out_error) const
{
	args_ = vcd_file_info_args{};
	std::string untrusted_path;
	if (raw_json.contains("path") && raw_json["path"].is_string()) {
		untrusted_path = raw_json["path"].get<std::string>();
	}
	args_.requested_path = verilog::verilog_module_scanner::resolve_vcd_path(
		ctx.fs_security.get_working_directory(), untrusted_path
	);

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

	if (raw_json.contains("stats") && raw_json["stats"].is_boolean()) {
		args_.stats = raw_json["stats"].get<bool>();
	}

	if (raw_json.contains("start_tick")) {
		std::string num_err;
		size_t st = 0;
		if (json_utils::get_number(raw_json, "start_tick", st, static_cast<size_t>(0), num_err)) {
			args_.start_tick = st;
			args_.stats = true;
		}
	}

	if (raw_json.contains("end_tick")) {
		std::string num_err;
		size_t et = 0;
		if (json_utils::get_number(raw_json, "end_tick", et, static_cast<size_t>(0), num_err)) {
			args_.end_tick = et;
			args_.stats = true;
		}
	}

	if (args_.start_tick && args_.end_tick && *args_.start_tick > *args_.end_tick) {
		out_error = std::format("start_tick ({}) cannot be greater than end_tick ({})",
			*args_.start_tick, *args_.end_tick);
		return false;
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
