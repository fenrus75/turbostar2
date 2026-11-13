#include "plugins/verilog/verilog_pnr_timing_validator.h"
#include "agentlib/tool_registry.h"
#include "fs_utils.h"

namespace tools
{

std::string verilog_pnr_timing_validator::get_description() const
{
	return "Analyzes physical place-and-route timing (nextpnr report), reporting achieved Fmax, "
	       "clock domain slack, hardware device utilization, and critical paths decomposed into "
	       "logic delay vs routing wire delay. Correlates physical cells and nets back to original "
	       "SystemVerilog file:line anchors. Focuses on Reg -> Reg clock-limiting paths by default. "
	       "Accepts pre-existing reports or runs automated on-demand synthesis + P&R using turbostar.json settings.";
}

nlohmann::json verilog_pnr_timing_validator::get_parameters_schema() const
{
	return nlohmann::json{
		{"type", "object"},
		{"properties", {
			{"timing_report", {
				{"type", "string"},
				{"description", "Optional path to pre-generated nextpnr timing report (e.g. 'synth/timing.json')."}
			}},
			{"netlist", {
				{"type", "string"},
				{"description", "Optional path to Yosys synthesis netlist JSON (e.g. 'synth/main.json') for cell-to-RTL correlation."}
			}},
			{"file", {
				{"type", "string"},
				{"description", "Optional RTL source file in the design to anchor on-demand synthesis scan (e.g. 'sv/main.sv')."}
			}},
			{"module", {
				{"type", "string"},
				{"description", "Optional target RTL module name or source file (e.g. 'top' or 'cpu'). Defaults to the configured top_module in turbostar.json; if a submodule is given, runs out-of-context unconstrained PnR for that submodule."}
			}},
			{"top_k", {
				{"type", "integer"},
				{"description", "Maximum number of worst critical paths or bottlenecks to display. Defaults to 3."}
			}},
			{"seeds", {
				{"type", "integer"},
				{"description", "Optional number of placement seeds (accepted for compatibility with verilog_compile; default: 1)."}
			}},
			{"group_by_sink", {
				{"type", "boolean"},
				{"description", "Whether to collapse redundant paths converging on the same bottleneck sink into single entries (default: true)."}
			}},
			{"path_filter", {
				{"type", "string"},
				{"enum", {"reg_to_reg", "all"}},
				{"description", "Path category filter: 'reg_to_reg' to focus exclusively on internal clock-limiting register paths (default), or 'all' to include async IO pads."}
			}},
			{"collapse", {
				{"type", "boolean"},
				{"description", "Whether to collapse noisy multi-hop carry chains and intermediate techmap routing hops into concise stages with logic/routing breakdown (default: true)."}
			}},
			{"verbose", {
				{"type", "boolean"},
				{"description", "When true (or show_segments=true), prints the full segment-by-segment critical path (cell type, input/output pins, tile coordinates, and intermediate nets) in addition to the stage summary (default: false)."}
			}},
			{"show_segments", {
				{"type", "boolean"},
				{"description", "Alias for verbose: when true, prints the full hop-by-hop segment list (cell type, input/output pins, and intermediate nets) for each critical path (default: false)."}
			}},
			{"show_code", {
				{"type", "integer"},
				{"description", "Number of RTL source code lines to display per hop/endpoint: 1 (default) shows a concise 1-line statement snippet inline, >1 prints a multi-line code block of up to N lines, and 0 (or false) disables code snippets."}
			}}
		}},
		{"required", nlohmann::json::array()}
	};
}

std::unordered_map<std::string, std::string> verilog_pnr_timing_validator::get_custom_parameter_aliases() const
{
	return {
		{"top_module", "module"},
		{"rtl_module", "module"},
		{"top", "module"},
		{"seed", "seeds"},
		{"show_segments", "verbose"},
		{"segments", "verbose"},
		{"detailed", "verbose"},
		{"full_path", "verbose"},
		{"code_lines", "show_code"},
		{"snippet_lines", "show_code"},
		{"include_code", "show_code"}
	};
}

bool verilog_pnr_timing_validator::validate_args_impl(const nlohmann::json &untrusted_args,
                                                     const agentlib::tool_context &ctx,
                                                     std::string &out_error) const
{
	args_ = verilog_pnr_timing_args{};

	if (untrusted_args.contains("timing_report") && untrusted_args["timing_report"].is_string()) {
		std::string untrusted_path = untrusted_args["timing_report"].get<std::string>();
		args_.requested_timing_report = untrusted_path;
		if (!untrusted_path.empty()) {
			std::string canon;
			if (!ctx.fs_security.validate_access(untrusted_path, agentlib::access_type::read, canon, out_error)) {
				return false;
			}
			args_.safe_timing_report = canon;
		}
	}

	if (untrusted_args.contains("netlist") && untrusted_args["netlist"].is_string()) {
		std::string untrusted_netlist = untrusted_args["netlist"].get<std::string>();
		args_.requested_netlist = untrusted_netlist;
		if (!untrusted_netlist.empty()) {
			std::string canon;
			if (!ctx.fs_security.validate_access(untrusted_netlist, agentlib::access_type::read, canon, out_error)) {
				return false;
			}
			args_.safe_netlist = canon;
		}
	}

	if (untrusted_args.contains("file") && untrusted_args["file"].is_string()) {
		std::string untrusted_file = untrusted_args["file"].get<std::string>();
		args_.requested_file = untrusted_file;
		if (!untrusted_file.empty()) {
			std::string canon;
			if (!ctx.fs_security.validate_access(untrusted_file, agentlib::access_type::read, canon, out_error)) {
				return false;
			}
			args_.safe_file = canon;
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
			std::string canon;
			std::string dummy_err;
			if (args_.safe_file.empty() &&
			    ctx.fs_security.validate_access(untrusted_mod, agentlib::access_type::read, canon, dummy_err) &&
			    fs_utils::is_regular_file(canon)) {
				args_.safe_file = canon;
			}
		}
	}

	if (untrusted_args.contains("seeds") && untrusted_args["seeds"].is_number_integer()) {
		int s = untrusted_args["seeds"].get<int>();
		if (s > 0) {
			args_.seeds = s;
		}
	}

	if (untrusted_args.contains("top_k") && untrusted_args["top_k"].is_number_integer()) {
		int k = untrusted_args["top_k"].get<int>();
		if (k > 0) {
			args_.top_k = static_cast<size_t>(k);
		}
	}

	if (untrusted_args.contains("group_by_sink") && untrusted_args["group_by_sink"].is_boolean()) {
		args_.group_by_sink = untrusted_args["group_by_sink"].get<bool>();
	}

	if (untrusted_args.contains("collapse") && untrusted_args["collapse"].is_boolean()) {
		args_.collapse = untrusted_args["collapse"].get<bool>();
	}

	if (untrusted_args.contains("verbose") && untrusted_args["verbose"].is_boolean()) {
		args_.verbose = untrusted_args["verbose"].get<bool>();
	}
	if (untrusted_args.contains("show_segments") && untrusted_args["show_segments"].is_boolean()) {
		args_.verbose = untrusted_args["show_segments"].get<bool>();
	}

	if (untrusted_args.contains("show_code")) {
		if (untrusted_args["show_code"].is_boolean()) {
			args_.show_code = untrusted_args["show_code"].get<bool>() ? 1 : 0;
		} else if (untrusted_args["show_code"].is_number_integer()) {
			int sc = untrusted_args["show_code"].get<int>();
			args_.show_code = (sc < 0) ? 0 : std::min(sc, 50);
		}
	}

	if (untrusted_args.contains("path_filter") && untrusted_args["path_filter"].is_string()) {
		std::string pf = untrusted_args["path_filter"].get<std::string>();
		if (pf == "all") {
			args_.reg_to_reg_only = false;
		} else if (pf == "reg_to_reg") {
			args_.reg_to_reg_only = true;
		} else {
			out_error = "Parameter 'path_filter' must be 'reg_to_reg' or 'all'";
			return false;
		}
	}

	return true;
}

std::unique_ptr<agentlib::llm_tool> verilog_pnr_timing_validator::create_tool_impl(const nlohmann::json &/*untrusted_args*/) const
{
	return std::make_unique<verilog_pnr_timing_tool>(args_);
}

} // namespace tools

extern "C" {

void register_verilog_pnr_timing(void)
{
	agentlib::tool_registry::get_instance().register_validator(
		[]() { return std::make_unique<tools::verilog_pnr_timing_validator>(); }
	);
}

void unregister_verilog_pnr_timing(void)
{
	agentlib::tool_registry::get_instance().unregister_validator("verilog_pnr_timing");
}

}
