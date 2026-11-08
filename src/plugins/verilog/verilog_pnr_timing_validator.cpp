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
			{"top_k", {
				{"type", "integer"},
				{"description", "Maximum number of worst critical paths or bottlenecks to display. Defaults to 3."}
			}},
			{"group_by_sink", {
				{"type", "boolean"},
				{"description", "Whether to collapse redundant paths converging on the same bottleneck sink into single entries (default: true)."}
			}},
			{"path_filter", {
				{"type", "string"},
				{"enum", {"reg_to_reg", "all"}},
				{"description", "Path category filter: 'reg_to_reg' to focus exclusively on internal clock-limiting register paths (default), or 'all' to include async IO pads."}
			}}
		}},
		{"required", nlohmann::json::array()}
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

	if (untrusted_args.contains("top_k") && untrusted_args["top_k"].is_number_integer()) {
		int k = untrusted_args["top_k"].get<int>();
		if (k > 0) {
			args_.top_k = static_cast<size_t>(k);
		}
	}

	if (untrusted_args.contains("group_by_sink") && untrusted_args["group_by_sink"].is_boolean()) {
		args_.group_by_sink = untrusted_args["group_by_sink"].get<bool>();
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
