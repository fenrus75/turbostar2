#include "vcd_wave_validator.h"
#include "agentlib/json_utils.h"
#include "agentlib/tool_registry.h"
#include <format>

namespace tools
{

std::string vcd_wave_validator::get_description() const
{
	return "Samples and formats VCD simulation signals into a cycle-by-cycle markdown waveform table "
	       "aligned with clock transitions (posedge, negedge, or both). "
	       "Supports hexadecimal (default for multi-bit, or 'x:name') and binary ('b:name') formats.";
}

nlohmann::json vcd_wave_validator::get_parameters_schema() const
{
	return {
		{"type", "object"},
		{"properties", {
			{"path", {
				{"type", "string"},
				{"description", "Path to the VCD file."}
			}},
			{"signals", {
				{"type", "array"},
				{"items", {{"type", "string"}}},
				{"description", "List of signal names or paths to sample. Can use 'b:signal' for binary or 'x:signal' for hex."}
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
			}},
			{"max_ticks", {
				{"type", "integer"},
				{"description", "Maximum number of clock ticks to sample in table (default: 50)."}
			}}
		}},
		{"required", {"path", "signals"}}
	};
}

bool vcd_wave_validator::validate_args_impl(const nlohmann::json &raw_json,
					    const agentlib::tool_context &ctx,
					    std::string &out_error) const
{
	if (!raw_json.contains("path") || !raw_json["path"].is_string()) {
		out_error = "Missing required string argument 'path'";
		return false;
	}

	if (!raw_json.contains("signals") || !raw_json["signals"].is_array() || raw_json["signals"].empty()) {
		out_error = "Missing required non-empty array argument 'signals'";
		return false;
	}

	args_ = vcd_wave_args{};
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

	for (const auto &item : raw_json["signals"]) {
		if (item.is_string()) {
			std::string s = item.get<std::string>();
			if (!s.empty()) {
				args_.signals.push_back(std::move(s));
			}
		}
	}

	if (args_.signals.empty()) {
		out_error = "Argument 'signals' must contain at least one valid signal name";
		return false;
	}

	if (raw_json.contains("clock_signal") && raw_json["clock_signal"].is_string()) {
		args_.clock_signal = raw_json["clock_signal"].get<std::string>();
	}

	if (raw_json.contains("edge") && raw_json["edge"].is_string()) {
		std::string e = raw_json["edge"].get<std::string>();
		if (e == "posedge" || e == "negedge" || e == "both") {
			args_.edge = std::move(e);
		} else {
			out_error = "Invalid value for 'edge'. Must be 'posedge', 'negedge', or 'both'";
			return false;
		}
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

	if (raw_json.contains("max_ticks")) {
		std::string num_err;
		size_t val = 50;
		if (json_utils::get_number(raw_json, "max_ticks", val, static_cast<size_t>(50), num_err) && val > 0) {
			args_.max_ticks = val;
		}
	}

	return true;
}

std::unique_ptr<agentlib::llm_tool> vcd_wave_validator::create_tool_impl(const nlohmann::json &/*raw_json*/) const
{
	return std::make_unique<vcd_wave_tool>(args_);
}

} // namespace tools

extern "C" {

void register_vcd_wave(void)
{
	agentlib::tool_registry::get_instance().register_validator(
		[]() { return std::make_unique<tools::vcd_wave_validator>(); }
	);
}

void unregister_vcd_wave(void)
{
	agentlib::tool_registry::get_instance().unregister_validator("vcd_wave");
}

}
