#include "vcd_signal_labels_validator.h"
#include "agentlib/json_utils.h"
#include "agentlib/tool_registry.h"
#include <format>

namespace tools
{

std::string vcd_signal_labels_validator::get_description() const
{
	return "Registers human-readable labels for specific signal values in a VCD file "
	       "(e.g. mapping opcodes, bus states, or FSM values like '0x06' -> 'LAI'). "
	       "Labels persist across calls for the VCD file (saved to a '<path>.labels.json' sidecar file) "
	       "and are automatically displayed in 'vcd_wave' tables. Use 'clear: true' to remove labels.";
}

nlohmann::json vcd_signal_labels_validator::get_parameters_schema() const
{
	return {
		{"type", "object"},
		{"properties", {
			{"path", {
				{"type", "string"},
				{"description", "Path to the VCD file."}
			}},
			{"signal", {
				{"type", "string"},
				{"description", "Signal name (e.g. 'opcode', 'state', or 'tb_main.uut.cpu8008.opcode')."}
			}},
			{"labels", {
				{"type", "object"},
				{"description", "Key-value map of signal values (hex '0x06', decimal '6', or binary '00000110') to string labels (e.g. 'LAI')."}
			}},
			{"clear", {
				{"type", "boolean"},
				{"description", "If true, clears labels for the specified signal (or all signals if signal is omitted)."}
			}}
		}},
		{"required", {"path"}}
	};
}

bool vcd_signal_labels_validator::validate_args_impl(const nlohmann::json &raw_json,
						    const agentlib::tool_context &ctx,
						    std::string &out_error) const
{
	if (!raw_json.contains("path") || !raw_json["path"].is_string()) {
		out_error = "Missing required string argument 'path'";
		return false;
	}

	args_ = vcd_signal_labels_args{};
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

	if (raw_json.contains("clear") && raw_json["clear"].is_boolean()) {
		args_.clear = raw_json["clear"].get<bool>();
	}

	if (raw_json.contains("signal") && raw_json["signal"].is_string()) {
		args_.signal = raw_json["signal"].get<std::string>();
	}

	if (!args_.clear && args_.signal.empty()) {
		out_error = "Argument 'signal' is required when registering labels";
		return false;
	}

	if (raw_json.contains("labels") && raw_json["labels"].is_object()) {
		for (auto &[k, v] : raw_json["labels"].items()) {
			if (v.is_string()) {
				args_.labels[k] = v.get<std::string>();
			}
		}
	}

	if (!args_.clear && args_.labels.empty()) {
		out_error = "Argument 'labels' must contain at least one label mapping";
		return false;
	}

	return true;
}

std::unique_ptr<agentlib::llm_tool> vcd_signal_labels_validator::create_tool_impl(const nlohmann::json &/*raw_json*/) const
{
	return std::make_unique<vcd_signal_labels_tool>(args_);
}

} // namespace tools

extern "C" {

void register_vcd_signal_labels(void)
{
	agentlib::tool_registry::get_instance().register_validator(
		[]() { return std::make_unique<tools::vcd_signal_labels_validator>(); }
	);
}

void unregister_vcd_signal_labels(void)
{
	agentlib::tool_registry::get_instance().unregister_validator("vcd_signal_labels");
}

}
