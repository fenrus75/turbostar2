#include "plugins/verilog/verilog_compile_validator.h"

#include "agentlib/tool_registry.h"
#include "fs_utils.h"

namespace tools
{

std::string verilog_compile_validator::get_description() const
{
	return "Compiles and verifies SystemVerilog/Verilog RTL health using Verilator lint, Yosys synthesis, and "
	       "nextpnr place-and-route. Writes the full raw compilation/synthesis log to `output_file` "
	       "(default: 'tmp://synthesis.log') and returns a structured diagnostic summary of errors, warnings, "
	       "inferred latches, combinational loops, resource utilization, and timing.";
}

nlohmann::json verilog_compile_validator::get_parameters_schema() const
{
	return nlohmann::json{
		{"type", "object"},
		{"properties", {
			{"module", {
				{"type", "string"},
				{"description", "Optional target RTL module name or source file path (e.g. 'main', 'alu_mod', or 'sv/alu/alu.sv'). Defaults to 'top_module' configured in turbostar.json."}
			}},
			{"lint_only", {
				{"type", "boolean"},
				{"description", "When true, runs Verilator in '--lint-only -Wall' mode for fast syntax, width, and lint checking without running Yosys or nextpnr (default: false)."}
			}},
			{"synth_only", {
				{"type", "boolean"},
				{"description", "When true, runs Verilator lint and Yosys synthesis (checking for inferred latches, combinational loops, and cell counts) without running nextpnr place-and-route (default: false)."}
			}},
			{"output_file", {
				{"type", "string"},
				{"description", "Path or VFS URI where the full raw compiler, synthesis, and place-and-route output log is saved (default: 'tmp://synthesis.log')."}
			}}
		}},
		{"required", nlohmann::json::array()}
	};
}

std::unordered_map<std::string, std::string> verilog_compile_validator::get_custom_parameter_aliases() const
{
	return {
		{"file", "module"},
		{"path", "module"},
		{"top_module", "module"},
		{"top", "module"},
		{"rtl_module", "module"},
		{"log_file", "output_file"},
		{"log", "output_file"},
		{"out", "output_file"}
	};
}

bool verilog_compile_validator::validate_args_impl(const nlohmann::json &untrusted_args,
                                                   const agentlib::tool_context &ctx,
                                                   std::string &out_error) const
{
	args_ = verilog_compile_args{};

	if (untrusted_args.contains("module") && untrusted_args["module"].is_string()) {
		std::string untrusted_mod = untrusted_args["module"].get<std::string>();
		if (!untrusted_mod.empty()) {
			if (!fs_utils::is_safe_for_ui(untrusted_mod)) {
				out_error = "Invalid characters in 'module' parameter";
				return false;
			}
			args_.requested_module = untrusted_mod;
			if (untrusted_mod.find('/') != std::string::npos ||
			    untrusted_mod.ends_with(".sv") || untrusted_mod.ends_with(".v")) {
				std::string safe_file;
				if (!ctx.fs_security.validate_access(untrusted_mod, agentlib::access_type::read, safe_file, out_error)) {
					return false;
				}
				if (!fs_utils::is_regular_file(safe_file)) {
					out_error = "Module file path is not a regular file: " + untrusted_mod;
					return false;
				}
				args_.safe_file = safe_file;
			}
		}
	}

	if (untrusted_args.contains("lint_only") && untrusted_args["lint_only"].is_boolean()) {
		args_.lint_only = untrusted_args["lint_only"].get<bool>();
	}

	if (untrusted_args.contains("synth_only") && untrusted_args["synth_only"].is_boolean()) {
		args_.synth_only = untrusted_args["synth_only"].get<bool>();
	}

	std::string untrusted_out = "tmp://synthesis.log";
	if (untrusted_args.contains("output_file") && untrusted_args["output_file"].is_string()) {
		std::string raw_out = untrusted_args["output_file"].get<std::string>();
		if (!raw_out.empty()) {
			untrusted_out = raw_out;
		}
	}

	if (!fs_utils::is_safe_for_ui(untrusted_out)) {
		out_error = "Invalid characters in 'output_file' parameter";
		return false;
	}

	std::string safe_out;
	if (!ctx.fs_security.validate_access(untrusted_out, agentlib::access_type::write, safe_out, out_error)) {
		return false;
	}
	args_.requested_output_file = untrusted_out;
	args_.safe_output_file = safe_out;

	return true;
}

std::unique_ptr<agentlib::llm_tool> verilog_compile_validator::create_tool_impl(const nlohmann::json & /*untrusted_args*/) const
{
	return std::make_unique<verilog_compile_tool>(args_);
}

} // namespace tools

extern "C" {

void register_verilog_compile(void)
{
	agentlib::tool_registry::get_instance().register_validator(
		[]() { return std::make_unique<tools::verilog_compile_validator>(); }
	);
}

void unregister_verilog_compile(void)
{
	agentlib::tool_registry::get_instance().unregister_validator("verilog_compile");
}

}
