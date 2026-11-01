#include "vcd_compare_validator.h"
#include "agentlib/json_utils.h"
#include "agentlib/tool_registry.h"
#include <format>

namespace tools
{

std::string vcd_compare_validator::get_description() const
{
	return "Compares two VCD simulation waveform traces clock-cycle by clock-cycle, detecting the first cycle of divergence "
	       "and listing differing signals. Supports common-signal intersection or strict mode, and respects validity qualifiers.";
}

nlohmann::json vcd_compare_validator::get_parameters_schema() const
{
	return {
		{"type", "object"},
		{"properties", {
			{"path_a", {
				{"type", "string"},
				{"description", "Path to first VCD file (reference / golden trace)."}
			}},
			{"path_b", {
				{"type", "string"},
				{"description", "Path to second VCD file (candidate / test run trace)."}
			}},
			{"signals", {
				{"type", "array"},
				{"items", {{"type", "string"}}},
				{"description", "Optional list of signal names or wildcard patterns (*, ?) to compare. If omitted, compares all common signals."}
			}},
			{"scope", {
				{"type", "string"},
				{"description", "Optional scope prefix to filter signals (e.g. 'tb_main.uut')."}
			}},
			{"clock_signal", {
				{"type", "string"},
				{"description", "Optional clock signal to sample on. If omitted, auto-detects from reference trace."}
			}},
			{"edge", {
				{"type", "string"},
				{"enum", {"posedge", "negedge", "both"}},
				{"description", "Clock transition edge to sample on: 'posedge' (default), 'negedge', or 'both'."}
			}},
			{"start_tick", {
				{"type", "integer"},
				{"description", "Starting clock tick index (default: 0, useful to skip reset sequence)."}
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
			}},
			{"strict", {
				{"type", "boolean"},
				{"description", "If true, fails if signal manifests differ between files. If false (default), compares intersection of common signals."}
			}},
			{"ignore_invalid", {
				{"type", "boolean"},
				{"description", "If true (default), ignores differences on clock cycles where signals are masked as invalid ('-') by registered qualifiers."}
			}}
		}},
		{"required", {"path_a", "path_b"}}
	};
}

bool vcd_compare_validator::validate_args_impl(/* untrusted */ const nlohmann::json &raw_json,
					      const agentlib::tool_context &ctx,
					      std::string &out_error) const
{
	if (!raw_json.contains("path_a") || !raw_json["path_a"].is_string() || raw_json["path_a"].get<std::string>().empty()) {
		out_error = "Missing required non-empty string argument 'path_a'";
		return false;
	}

	if (!raw_json.contains("path_b") || !raw_json["path_b"].is_string() || raw_json["path_b"].get<std::string>().empty()) {
		out_error = "Missing required non-empty string argument 'path_b'";
		return false;
	}

	args_ = vcd_compare_args{};
	args_.requested_path_a = raw_json["path_a"].get<std::string>();
	args_.requested_path_b = raw_json["path_b"].get<std::string>();

	std::string untrusted_path_a = args_.requested_path_a;
	std::string safe_path_a;
	std::string sec_err_a;
	if (!ctx.fs_security.validate_access(untrusted_path_a, agentlib::access_type::read, safe_path_a, sec_err_a)) {
		out_error = std::format("Access denied to path_a '{}': {}", untrusted_path_a, sec_err_a);
		return false;
	}
	args_.safe_path_a = safe_path_a;

	std::string untrusted_path_b = args_.requested_path_b;
	std::string safe_path_b;
	std::string sec_err_b;
	if (!ctx.fs_security.validate_access(untrusted_path_b, agentlib::access_type::read, safe_path_b, sec_err_b)) {
		out_error = std::format("Access denied to path_b '{}': {}", untrusted_path_b, sec_err_b);
		return false;
	}
	args_.safe_path_b = safe_path_b;

	if (raw_json.contains("signals") && raw_json["signals"].is_array()) {
		for (const auto &item : raw_json["signals"]) {
			if (item.is_string() && !item.get<std::string>().empty()) {
				args_.signals.push_back(item.get<std::string>());
			}
		}
	}

	if (raw_json.contains("scope") && raw_json["scope"].is_string()) {
		args_.scope = raw_json["scope"].get<std::string>();
	}

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

	if (raw_json.contains("strict") && raw_json["strict"].is_boolean()) {
		args_.strict = raw_json["strict"].get<bool>();
	}

	if (raw_json.contains("ignore_invalid") && raw_json["ignore_invalid"].is_boolean()) {
		args_.ignore_invalid = raw_json["ignore_invalid"].get<bool>();
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

std::unique_ptr<agentlib::llm_tool> vcd_compare_validator::create_tool_impl(const nlohmann::json &/*raw_json*/) const
{
	return std::make_unique<vcd_compare_tool>(args_);
}

} // namespace tools

extern "C"
{

void register_vcd_compare()
{
	agentlib::tool_registry::get_instance().register_validator(
		[]() { return std::make_unique<tools::vcd_compare_validator>(); });
}

void unregister_vcd_compare()
{
	agentlib::tool_registry::get_instance().unregister_validator("vcd_compare");
}

}
