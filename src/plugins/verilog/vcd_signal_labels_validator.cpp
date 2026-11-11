#include "vcd_signal_labels_validator.h"
#include "agentlib/json_utils.h"
#include "agentlib/tool_registry.h"
#include "plugins/verilog/verilog_module_scanner.h"
#include <format>
namespace tools
{

std::string vcd_signal_labels_validator::get_description() const
{
	return "Registers or queries human-readable labels for specific signal values in VCD waveforms "
	       "(e.g. mapping opcodes, bus states, or FSM values like '0x06' -> 'LAI', or binary masks like '00????10'). "
	       "Signal names support wildcards ('*' and '?') such as 'uut.*.opcode' to label signals across hierarchy levels. "
	       "If 'labels' is omitted, lists all currently active label mappings for the file. "
	       "By default ('global: true'), labels are saved to project-wide 'vcd_labels.json' so they apply across all testbenches and VCD files "
	       "(or set 'global: false' to save to '<path>.labels.json'). Use 'clear: true' to remove labels.";
}

nlohmann::json vcd_signal_labels_validator::get_parameters_schema() const
{
	return {
		{"type", "object"},
		{"properties", {
			{"path", {
				{"type", "string"},
				{"description", "Optional path to the VCD file. If omitted, automatically resolved from the project's testbench."}
			}},
			{"signal", {
				{"type", "string"},
				{"description", "Optional signal name or wildcard pattern (e.g. 'opcode', '*state*', or 'uut.*.opcode'). Required when registering labels."}
			}},
			{"labels", {
				{"type", "object"},
				{"description", "Optional key-value map of signal values (hex '0x06', decimal '6', or binary with wildcards 'b01???01') to string labels (e.g. 'LAI'). If omitted and clear is false, lists current labels."}
			}},
			{"clear", {
				{"type", "boolean"},
				{"description", "If true, clears labels for the specified signal (or all signals if signal is omitted)."}
			}},
			{"global", {
				{"type", "boolean"},
				{"description", "If true (default), saves labels to project-wide 'vcd_labels.json' so they apply across all testbenches and VCD files. Set to false to save only to '<path>.labels.json'."}
			}}
		}}
	};
}

bool vcd_signal_labels_validator::validate_args_impl(const nlohmann::json &raw_json,
						    const agentlib::tool_context &ctx,
						    std::string &out_error) const
{
	args_ = vcd_signal_labels_args{};
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

	if (raw_json.contains("clear") && raw_json["clear"].is_boolean()) {
		args_.clear = raw_json["clear"].get<bool>();
	}

	if (raw_json.contains("global") && raw_json["global"].is_boolean()) {
		args_.global = raw_json["global"].get<bool>();
	}

	if (raw_json.contains("signal") && raw_json["signal"].is_string()) {
		args_.signal = raw_json["signal"].get<std::string>();
	}

	if (raw_json.contains("labels") && raw_json["labels"].is_object()) {
		for (auto &[k, v] : raw_json["labels"].items()) {
			if (v.is_string()) {
				args_.labels[k] = v.get<std::string>();
			}
		}
	}

	// Validation rule:
	// If labels are provided, signal is required.
	if (!args_.labels.empty() && args_.signal.empty()) {
		out_error = "Argument 'signal' is required when registering labels";
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
