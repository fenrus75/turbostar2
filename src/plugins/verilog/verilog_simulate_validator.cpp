#include "plugins/verilog/verilog_simulate_validator.h"

#include <cctype>
#include "agentlib/tool_registry.h"
#include "fs_utils.h"
#include "plugins/verilog/fpga_project_settings.h"

namespace tools
{

std::string verilog_simulate_validator::get_description() const
{
	return "Compiles and runs one or more SystemVerilog/Verilog testbenches together with project RTL using Verilator, "
	       "supporting module-targeted unit testing (`module`), optional bottom-up submodule testing (`hierarchical`), "
	       "and test tier filtering (`level`: 'quick', 'comprehensive', 'extreme'). Generates VCD waveforms, "
	       "automatically compares against `.previous.vcd` and `.lastpass.vcd`, and returns pass/fail summaries.";
}

nlohmann::json verilog_simulate_validator::get_parameters_schema() const
{
	return nlohmann::json{
		{"type", "object"},
		{"properties", {
			{"testbench", {
				{"type", "string"},
				{"description", "Optional path to a specific testbench .sv/.v file (e.g. 'tb/tb_main.sv'). If omitted, uses 'module' associations or auto-discovers testbenches."}
			}},
			{"module", {
				{"type", "string"},
				{"description", "Optional RTL module name or source file path (e.g. 'alu_mod', 'alu', or 'sv/alu/alu.sv'). Runs all testbenches associated with this module (configured via verilog_configure_testbench or auto-discovered by instantiation)."}
			}},
			{"hierarchical", {
				{"type", "boolean"},
				{"description", "When true and 'module' is specified, also runs testbenches for all submodules instantiated transitively by 'module', deduplicated and ordered bottom-up (default: false)."}
			}},
			{"level", {
				{"type", "string"},
				{"enum", {"quick", "comprehensive", "extreme"}},
				{"description", "Cumulative test tier filter: 'quick' runs only quick tests; 'comprehensive' runs quick + comprehensive tests; 'extreme' runs quick + comprehensive + extreme tests (default: 'comprehensive')."}
			}},
			{"top_module", {
				{"type", "string"},
				{"description", "Optional top-level testbench module name (e.g. 'tb_main'). If omitted, automatically detected from each testbench file."}
			}},
			{"timeout_sec", {
				{"type", "integer"},
				{"description", "Maximum simulation runtime in seconds per testbench before timing out (default: 30)."}
			}}
		}},
		{"required", nlohmann::json::array()}
	};
}

std::unordered_map<std::string, std::string> verilog_simulate_validator::get_custom_parameter_aliases() const
{
	return {
		{"file", "testbench"},
		{"tb", "testbench"},
		{"testbench_file", "testbench"},
		{"path", "testbench"},
		{"rtl_module", "module"},
		{"tier", "level"},
		{"top", "top_module"},
		{"timeout", "timeout_sec"}
	};
}

bool verilog_simulate_validator::validate_args_impl(const nlohmann::json &untrusted_args,
                                                    const agentlib::tool_context &ctx,
                                                    std::string &out_error) const
{
	args_ = verilog_simulate_args{};

	if (untrusted_args.contains("testbench") && untrusted_args["testbench"].is_string()) {
		std::string untrusted_tb = untrusted_args["testbench"].get<std::string>();
		args_.requested_testbench = untrusted_tb;
		if (!untrusted_tb.empty()) {
			std::string safe_tb;
			if (!ctx.fs_security.validate_access(untrusted_tb, agentlib::access_type::read, safe_tb, out_error)) {
				return false;
			}
			if (!fs_utils::is_regular_file(safe_tb)) {
				out_error = "Testbench path is not a regular file: " + untrusted_tb;
				return false;
			}
			args_.safe_testbench = safe_tb;
		}
	}

	if (untrusted_args.contains("module") && untrusted_args["module"].is_string()) {
		std::string untrusted_mod = untrusted_args["module"].get<std::string>();
		if (!untrusted_mod.empty()) {
			if (!fs_utils::is_safe_for_ui(untrusted_mod)) {
				out_error = "Invalid characters in 'module' parameter";
				return false;
			}
			args_.requested_module = untrusted_mod;
		}
	}

	if (untrusted_args.contains("hierarchical") && untrusted_args["hierarchical"].is_boolean()) {
		args_.hierarchical = untrusted_args["hierarchical"].get<bool>();
		args_.hierarchical_explicit = true;
	}

	if (untrusted_args.contains("level") && untrusted_args["level"].is_string()) {
		std::string untrusted_lvl = untrusted_args["level"].get<std::string>();
		if (!untrusted_lvl.empty()) {
			std::string norm = verilog::fpga_project_settings::normalize_test_level(untrusted_lvl);
			if (norm.empty()) {
				out_error = "Invalid 'level' parameter: must be 'quick', 'comprehensive', or 'extreme'";
				return false;
			}
			args_.level = norm;
			args_.level_explicit = true;
		}
	}

	if (untrusted_args.contains("top_module") && untrusted_args["top_module"].is_string()) {
		std::string untrusted_top = untrusted_args["top_module"].get<std::string>();
		if (!untrusted_top.empty()) {
			if (!fs_utils::is_safe_for_ui(untrusted_top)) {
				out_error = "Invalid characters in top_module parameter";
				return false;
			}
			bool valid_ident = std::isalpha(static_cast<unsigned char>(untrusted_top[0])) || untrusted_top[0] == '_';
			for (char c : untrusted_top) {
				if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '$') {
					valid_ident = false;
					break;
				}
			}
			if (!valid_ident) {
				out_error = "top_module must be a valid Verilog identifier";
				return false;
			}
			args_.top_module = untrusted_top;
		}
	}

	if (untrusted_args.contains("timeout_sec") && untrusted_args["timeout_sec"].is_number_integer()) {
		int untrusted_timeout = untrusted_args["timeout_sec"].get<int>();
		if (untrusted_timeout < 1 || untrusted_timeout > 300) {
			out_error = "Parameter 'timeout_sec' must be between 1 and 300 seconds";
			return false;
		}
		args_.timeout_sec = untrusted_timeout;
	}

	return true;
}

std::unique_ptr<agentlib::llm_tool> verilog_simulate_validator::create_tool_impl(const nlohmann::json &/*untrusted_args*/) const
{
	return std::make_unique<verilog_simulate_tool>(args_);
}

} // namespace tools

extern "C" {

void register_verilog_simulate(void)
{
	agentlib::tool_registry::get_instance().register_validator(
		[]() { return std::make_unique<tools::verilog_simulate_validator>(); }
	);
}

void unregister_verilog_simulate(void)
{
	agentlib::tool_registry::get_instance().unregister_validator("verilog_simulate");
}

}
