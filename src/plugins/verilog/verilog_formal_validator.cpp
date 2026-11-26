#include "plugins/verilog/verilog_formal_validator.h"

#include <algorithm>
#include <ctre.hpp>
#include "agentlib/tool_registry.h"
#include "fs_utils.h"

namespace tools
{

std::string verilog_formal_validator::get_description() const
{
	return "Runs SymbiYosys (`sby`) formal verification (`bmc`, `prove`, `cover`, `prove+cover`, `bmc+cover`, or `all`) "
	       "on SystemVerilog/Verilog modules using `bind`-based property files in `formal/*_props.sv`. Automatically "
	       "resolves transitive submodules, stages `$readmem` data files, supports parameter overrides (`parameters`), "
	       "auto-detects SMT solvers (`bitwuzla`, `yices`, `z3`), reports per-module timing, flags vacuity risks, "
	       "expands failing/unreachable diagnostics inline (writing full coverpoint details to `system://verilog/formal.md`), "
	       "and copies counterexample VCDs for immediate `vcd_wave` inspection.";
}

nlohmann::json verilog_formal_validator::get_parameters_schema() const
{
	return nlohmann::json{
		{"type", "object"},
		{"properties", {
			{"module", {
				{"type", "string"},
				{"description", "Optional RTL module name (e.g. 'alu8', 'decode_mod'). If omitted, runs formal verification on all modules with bound property files in `formal/`."}
			}},
			{"mode", {
				{"type", "string"},
				{"enum", {"bmc", "prove", "cover", "prove+cover", "bmc+cover", "all"}},
				{"description", "Formal verification mode: 'bmc' (bounded model checking), 'prove' (k-induction unbounded proof + basecase), 'cover' (coverpoint reachability), 'prove+cover', 'bmc+cover', or 'all' (runs prove + cover; default: 'all')."}
			}},
			{"depth", {
				{"type", "integer"},
				{"description", "Maximum step depth for BMC, prove, and cover tasks (default: 20, range 1..500)."}
			}},
			{"solver", {
				{"type", "string"},
				{"enum", {"auto", "bitwuzla", "yices", "z3"}},
				{"description", "SMT solver for smtbmc engine. 'auto' picks the fastest installed solver from bitwuzla -> yices -> z3 (default: 'auto')."}
			}},
			{"timeout_sec", {
				{"type", "integer"},
				{"description", "Timeout in seconds per module (default: 60, range 5..600)."}
			}},
			{"parameters", {
				{"type", "object"},
				{"description", "Optional map of Verilog parameter overrides for the top module (e.g. {\"COUNTER_BITS\": \"4\"}), applied via read_slang -G / chparam."}
			}},
			{"verbose", {
				{"type", "boolean"},
				{"description", "When true, include all passing coverpoint tables inline. When false (default), only failing/unreachable coverpoints are expanded inline and the full report is written to `system://verilog/formal.md`."}
			}}
		}},
		{"required", nlohmann::json::array()}
	};
}

std::unordered_map<std::string, std::string> verilog_formal_validator::get_custom_parameter_aliases() const
{
	return {
		{"rtl_module", "module"},
		{"task", "mode"},
		{"steps", "depth"},
		{"timeout", "timeout_sec"},
		{"Parameters", "parameters"},
		{"chparam", "parameters"},
		{"Verbose", "verbose"}
	};
}

bool verilog_formal_validator::validate_args_impl(const nlohmann::json &untrusted_args,
                                                  const agentlib::tool_context &/*ctx*/,
                                                  std::string &out_error) const
{
	args_ = verilog_formal_args{};

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

	if (untrusted_args.contains("mode") && untrusted_args["mode"].is_string()) {
		std::string untrusted_mode = untrusted_args["mode"].get<std::string>();
		if (untrusted_mode != "bmc" && untrusted_mode != "prove" &&
		    untrusted_mode != "cover" && untrusted_mode != "prove+cover" &&
		    untrusted_mode != "bmc+cover" && untrusted_mode != "all") {
			out_error = "Parameter 'mode' must be one of: 'bmc', 'prove', 'cover', 'prove+cover', 'bmc+cover', 'all'";
			return false;
		}
		args_.mode = untrusted_mode;
	}

	if (untrusted_args.contains("depth") && untrusted_args["depth"].is_number_integer()) {
		int untrusted_depth = untrusted_args["depth"].get<int>();
		args_.depth = std::clamp(untrusted_depth, 1, 500);
	}

	if (untrusted_args.contains("solver") && untrusted_args["solver"].is_string()) {
		std::string untrusted_solver = untrusted_args["solver"].get<std::string>();
		if (untrusted_solver != "auto" && untrusted_solver != "bitwuzla" &&
		    untrusted_solver != "yices" && untrusted_solver != "z3") {
			out_error = "Parameter 'solver' must be one of: 'auto', 'bitwuzla', 'yices', 'z3'";
			return false;
		}
		args_.solver = untrusted_solver;
	}

	if (untrusted_args.contains("timeout_sec") && untrusted_args["timeout_sec"].is_number_integer()) {
		int untrusted_to = untrusted_args["timeout_sec"].get<int>();
		args_.timeout_sec = std::clamp(untrusted_to, 5, 600);
	}

	if (untrusted_args.contains("parameters")) {
		if (!untrusted_args["parameters"].is_object()) {
			out_error = "Parameter 'parameters' must be a JSON object mapping parameter names to values";
			return false;
		}
		for (const auto &[untrusted_k, untrusted_v] : untrusted_args["parameters"].items()) {
			if (!ctre::match<R"([a-zA-Z_][a-zA-Z_0-9]*)">(untrusted_k)) {
				out_error = "Invalid Verilog parameter name in 'parameters'";
				return false;
			}
			std::string untrusted_val;
			if (untrusted_v.is_string()) {
				untrusted_val = untrusted_v.get<std::string>();
			} else if (untrusted_v.is_number_integer()) {
				untrusted_val = std::to_string(untrusted_v.get<long long>());
			} else {
				out_error = "Parameter values in 'parameters' must be integers or strings";
				return false;
			}
			if (untrusted_val.empty() || !ctre::match<R"([0-9a-zA-Z_'+\-]+)">(untrusted_val)) {
				out_error = "Invalid Verilog parameter literal in 'parameters'";
				return false;
			}
			args_.parameters[untrusted_k] = untrusted_val;
		}
	}

	if (untrusted_args.contains("verbose") && untrusted_args["verbose"].is_boolean()) {
		args_.verbose = untrusted_args["verbose"].get<bool>();
	}

	return true;
}

std::unique_ptr<agentlib::llm_tool> verilog_formal_validator::create_tool_impl(const nlohmann::json &/*untrusted_args*/) const
{
	return std::make_unique<verilog_formal_tool>(args_);
}

} // namespace tools

extern "C" {

void register_verilog_formal(void)
{
	agentlib::tool_registry::get_instance().register_validator(
		[]() { return std::make_unique<tools::verilog_formal_validator>(); }
	);
}

void unregister_verilog_formal(void)
{
	agentlib::tool_registry::get_instance().unregister_validator("verilog_formal");
}

}
