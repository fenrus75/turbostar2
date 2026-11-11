#include "vcd_signal_qualifier_validator.h"
#include "agentlib/tool_registry.h"
#include "plugins/verilog/verilog_module_scanner.h"
#include <format>
namespace tools
{

std::string vcd_signal_qualifier_validator::get_description() const
{
	return "Declares or queries validity qualifier relationships between signals (e.g. data bus qualified by valid bit, "
	       "or active-low '~cs_n'). Masks invalid cycles with '-' in vcd_wave, prevents noise from breaking idle collapsing, "
	       "and filters out invalid cycles in vcd_search. By default ('global: true'), qualifiers are saved to project-wide 'vcd_labels.json'.";
}

nlohmann::json vcd_signal_qualifier_validator::get_parameters_schema() const
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
				{"description", "Signal name or wildcard pattern to qualify (e.g. 'opcode_data' or 'uut.*.data')."}
			}},
			{"qualifier", {
				{"type", "string"},
				{"description", "1-bit qualifier signal name. Prefix with '~' or '!' for active-low (e.g. 'valid' or '~cs_n')."}
			}},
			{"bindings", {
				{"type", "object"},
				{"additionalProperties", {{"type", "string"}}},
				{"description", "Batch map of signal names to qualifier names (e.g. {'wdata': 'we', 'rdata': 're'})."}
			}},
			{"clear", {
				{"type", "boolean"},
				{"description", "If true, clears qualifiers for the specified signal, or all qualifiers if signal is omitted."}
			}},
			{"global", {
				{"type", "boolean"},
				{"description", "If true (default), saves qualifiers to project-wide 'vcd_labels.json' so they apply across all testbenches and VCD files. Set to false to save only to '<path>.labels.json'."}
			}}
		}}
	};
}

bool vcd_signal_qualifier_validator::validate_args_impl(const nlohmann::json &raw_json,
							const agentlib::tool_context &ctx,
							std::string &out_error) const
{
	args_ = vcd_signal_qualifier_args{};
	std::string raw_path;
	if (raw_json.contains("path") && raw_json["path"].is_string()) {
		raw_path = raw_json["path"].get<std::string>();
	}
	args_.requested_path = verilog::verilog_module_scanner::resolve_vcd_path(
		ctx.fs_security.get_working_directory(), raw_path
	);

	std::string untrusted_path = args_.requested_path;
	std::string safe_path;
	std::string sec_err;
	if (!ctx.fs_security.validate_access(untrusted_path, agentlib::access_type::read, safe_path, sec_err)) {
		out_error = std::format("Access denied to path '{}': {}", untrusted_path, sec_err);
		return false;
	}
	args_.safe_path = safe_path;

	if (raw_json.contains("signal") && raw_json["signal"].is_string()) {
		args_.signal = raw_json["signal"].get<std::string>();
	}

	if (raw_json.contains("qualifier") && raw_json["qualifier"].is_string()) {
		args_.qualifier = raw_json["qualifier"].get<std::string>();
	}

	if (raw_json.contains("bindings") && raw_json["bindings"].is_object()) {
		for (auto &[sig, q] : raw_json["bindings"].items()) {
			if (q.is_string()) {
				args_.bindings[sig] = q.get<std::string>();
			}
		}
	}

	args_.clear = raw_json.value("clear", false);
	args_.global = raw_json.value("global", true);

	return true;
}

std::unique_ptr<agentlib::llm_tool> vcd_signal_qualifier_validator::create_tool_impl(const nlohmann::json &/*raw_json*/) const
{
	return std::make_unique<vcd_signal_qualifier_tool>(args_);
}

} // namespace tools

extern "C"
{
void register_vcd_signal_qualifier(void)
{
	agentlib::tool_registry::get_instance().register_validator(
		[]() { return std::make_unique<tools::vcd_signal_qualifier_validator>(); }
	);
}

void unregister_vcd_signal_qualifier(void)
{
	agentlib::tool_registry::get_instance().unregister_validator("vcd_signal_qualifier");
}
}
