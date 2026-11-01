#include "vcd_search_validator.h"
#include "agentlib/json_utils.h"
#include "agentlib/tool_registry.h"
#include <format>

namespace tools
{

std::string vcd_search_validator::get_description() const
{
	return "Searches a VCD simulation trace for clock cycles matching signal values "
	       "(hexadecimal, binary with '?' wildcards, or label names), returning matched cycle intervals. "
	       "Supports single-signal queries via 'signal'/'value' or multi-signal simultaneous matching "
	       "via 'conditions' (key-value map of signals to values combined with logical AND).";
}

nlohmann::json vcd_search_validator::get_parameters_schema() const
{
	return {
		{"type", "object"},
		{"properties", {
			{"path", {
				{"type", "string"},
				{"description", "Path to the VCD file."}
			}},
			{"conditions", {
				{"type", "object"},
				{"description", "Key-value map of signal names to expected values to match simultaneously (logical AND), e.g. {\"valid\": \"1\", \"opcode\": \"0x36\"}. Can be used instead of 'signal' and 'value'."},
				{"additionalProperties", {{"type", "string"}}}
			}},
			{"signal", {
				{"type", "string"},
				{"description", "Signal name or path for single-signal search (supports short names, suffix matching, or 'b:'/'x:' prefixes)."}
			}},
			{"value", {
				{"type", "string"},
				{"description", "Value to match against for single-signal search: hex (e.g. '0x36'), binary with wildcards (e.g. 'b01???01'), or registered label name (e.g. 'LMA')."}
			}},
			{"clock_signal", {
				{"type", "string"},
				{"description", "Optional clock signal to sample on. If omitted, auto-detects the primary clock candidate."}
			}},
			{"edge", {
				{"type", "string"},
				{"enum", {"posedge", "negedge", "both"}},
				{"description", "Clock transition edge to sample on: 'posedge' (default), 'negedge', or 'both'."}
			}},
			{"direction", {
				{"type", "string"},
				{"enum", {"forward", "backward"}},
				{"description", "Search direction: 'forward' (default) or 'backward'."}
			}},
			{"limit", {
				{"type", "integer"},
				{"description", "Maximum number of matched intervals to return (default: 10)."}
			}},
			{"first", {
				{"type", "boolean"},
				{"description", "If true, shortcut to return only the first matched interval."}
			}},
			{"last", {
				{"type", "boolean"},
				{"description", "If true, shortcut to return only the last matched interval."}
			}},
			{"start_tick", {
				{"type", "integer"},
				{"description", "Starting clock tick index (default: 0)."}
			}},
			{"end_tick", {
				{"type", "integer"},
				{"description", "Ending clock tick index."}
			}},
			{"start_time", {
				{"type", "integer"},
				{"description", "Optional start time window in VCD timescale units."}
			}},
			{"end_time", {
				{"type", "integer"},
				{"description", "Optional end time window in VCD timescale units."}
			}}
		}},
		{"required", {"path"}}
	};
}

bool vcd_search_validator::validate_args_impl(/* untrusted */ const nlohmann::json &raw_json,
					      const agentlib::tool_context &ctx,
					      std::string &out_error) const
{
	if (!raw_json.contains("path") || !raw_json["path"].is_string()) {
		out_error = "Missing required string argument 'path'";
		return false;
	}

	args_ = vcd_search_args{};
	args_.requested_path = raw_json["path"].get<std::string>();

	std::string untrusted_path = args_.requested_path;
	std::string safe_path;
	std::string sec_err;
	if (!ctx.fs_security.validate_access(untrusted_path, agentlib::access_type::read, safe_path, sec_err)) {
		out_error = std::format("Access denied to path '{}': {}", untrusted_path, sec_err);
		return false;
	}
	args_.safe_path = safe_path;

	if (raw_json.contains("conditions")) {
		const auto &untrusted_conds = raw_json["conditions"];
		if (untrusted_conds.is_object()) {
			for (auto it = untrusted_conds.begin(); it != untrusted_conds.end(); ++it) {
				std::string untrusted_sig = it.key();
				if (untrusted_sig.empty()) {
					out_error = "Condition signal name cannot be empty";
					return false;
				}
				std::string untrusted_val;
				if (it.value().is_string()) {
					untrusted_val = it.value().get<std::string>();
				} else if (it.value().is_number_integer()) {
					untrusted_val = std::to_string(it.value().get<int64_t>());
				} else if (it.value().is_boolean()) {
					untrusted_val = it.value().get<bool>() ? "1" : "0";
				} else {
					out_error = std::format("Invalid value for condition '{}': expected string, number, or boolean", untrusted_sig);
					return false;
				}
				if (untrusted_val.empty()) {
					out_error = std::format("Condition value for signal '{}' cannot be empty", untrusted_sig);
					return false;
				}
				args_.conditions.push_back({untrusted_sig, untrusted_val});
			}
		} else if (untrusted_conds.is_array()) {
			for (const auto &item : untrusted_conds) {
				if (!item.is_object() || !item.contains("signal") || !item.contains("value")) {
					out_error = "Array condition items must be objects with 'signal' and 'value' fields";
					return false;
				}
				std::string untrusted_sig = item["signal"].is_string() ? item["signal"].get<std::string>() : "";
				std::string untrusted_val = item["value"].is_string() ? item["value"].get<std::string>() : "";
				if (untrusted_sig.empty() || untrusted_val.empty()) {
					out_error = "Condition 'signal' and 'value' must be non-empty strings";
					return false;
				}
				args_.conditions.push_back({untrusted_sig, untrusted_val});
			}
		} else {
			out_error = "Argument 'conditions' must be an object map (e.g. {\"signal\": \"value\"})";
			return false;
		}
	}

	if (raw_json.contains("signal") && raw_json["signal"].is_string() && !raw_json["signal"].get<std::string>().empty()) {
		std::string untrusted_sig = raw_json["signal"].get<std::string>();
		std::string untrusted_val = (raw_json.contains("value") && raw_json["value"].is_string()) ? raw_json["value"].get<std::string>() : "";
		if (untrusted_val.empty()) {
			out_error = "When 'signal' is specified, 'value' must also be specified and non-empty";
			return false;
		}
		bool already_present = false;
		for (const auto &c : args_.conditions) {
			if (c.signal == untrusted_sig) {
				already_present = true;
				break;
			}
		}
		if (!already_present) {
			args_.conditions.push_back({untrusted_sig, untrusted_val});
		}
	} else if (raw_json.contains("value") && (!raw_json.contains("signal") || raw_json["signal"].get<std::string>().empty())) {
		out_error = "When 'value' is specified, 'signal' must also be specified and non-empty";
		return false;
	}

	if (args_.conditions.empty()) {
		out_error = "Must provide either 'conditions' (key-value map of signals to values) or both 'signal' and 'value'";
		return false;
	}

	args_.signal = args_.conditions.front().signal;
	args_.value = args_.conditions.front().value;

	if (raw_json.contains("clock_signal") && raw_json["clock_signal"].is_string()) {
		args_.clock_signal = raw_json["clock_signal"].get<std::string>();
	}

	if (raw_json.contains("edge") && raw_json["edge"].is_string()) {
		std::string edge = raw_json["edge"].get<std::string>();
		if (edge == "posedge" || edge == "negedge" || edge == "both") {
			args_.edge = edge;
		} else {
			out_error = "Invalid 'edge' argument: must be 'posedge', 'negedge', or 'both'";
			return false;
		}
	}

	if (raw_json.contains("direction") && raw_json["direction"].is_string()) {
		std::string dir = raw_json["direction"].get<std::string>();
		if (dir == "forward" || dir == "backward") {
			args_.direction = dir;
		} else {
			out_error = "Invalid 'direction' argument: must be 'forward' or 'backward'";
			return false;
		}
	}

	if (raw_json.contains("limit")) {
		std::string num_err;
		size_t val = 10;
		if (json_utils::get_number(raw_json, "limit", val, static_cast<size_t>(10), num_err) && val > 0) {
			args_.limit = val;
		}
	}

	bool first_flag = raw_json.value("first", false);
	bool last_flag = raw_json.value("last", false);
	if (first_flag && last_flag) {
		out_error = "Cannot specify both 'first' and 'last' as true";
		return false;
	}
	if (first_flag) {
		args_.direction = "forward";
		args_.limit = 1;
	} else if (last_flag) {
		args_.direction = "backward";
		args_.limit = 1;
	}

	if (raw_json.contains("start_tick")) {
		std::string num_err;
		size_t val = 0;
		if (json_utils::get_number(raw_json, "start_tick", val, static_cast<size_t>(0), num_err)) {
			args_.start_tick = val;
		}
	}

	if (raw_json.contains("end_tick")) {
		std::string num_err;
		size_t val = 0;
		if (json_utils::get_number(raw_json, "end_tick", val, static_cast<size_t>(0), num_err)) {
			args_.end_tick = val;
		}
	}

	if (raw_json.contains("start_time")) {
		std::string num_err;
		uint64_t val = 0;
		if (json_utils::get_number(raw_json, "start_time", val, static_cast<uint64_t>(0), num_err)) {
			args_.start_time = val;
		}
	}

	if (raw_json.contains("end_time")) {
		std::string num_err;
		uint64_t val = 0;
		if (json_utils::get_number(raw_json, "end_time", val, static_cast<uint64_t>(0), num_err)) {
			args_.end_time = val;
		}
	}

	return true;
}

std::unique_ptr<agentlib::llm_tool> vcd_search_validator::create_tool_impl(const nlohmann::json &/*raw_json*/) const
{
	return std::make_unique<vcd_search_tool>(args_);
}

} // namespace tools

extern "C"
{

void register_vcd_search()
{
	agentlib::tool_registry::get_instance().register_validator(
		[]() { return std::make_unique<tools::vcd_search_validator>(); });
}

void unregister_vcd_search()
{
	agentlib::tool_registry::get_instance().unregister_validator("vcd_search");
}

}
