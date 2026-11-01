#include "vcd_signal_to_code_tool.h"
#include "yosys_netlist_reader.h"
#include <format>
#include <filesystem>

namespace tools
{

vcd_signal_to_code_tool::vcd_signal_to_code_tool(vcd_signal_to_code_args args)
	: agentlib::llm_tool_action("Tracing signal to source code"), args_(std::move(args))
{
}

bool vcd_signal_to_code_tool::validate_runtime(const agentlib::tool_context &/*ctx*/, std::string &/*out_error*/) const
{
	return true;
}

std::string vcd_signal_to_code_tool::execute(agentlib::tool_context &ctx)
{
	std::string safe_netlist = args_.safe_netlist;

	auto discover_netlist = [&ctx]() -> std::string {
		std::vector<std::string> candidates = {
			"tests/fixtures/test_netlist.json",
			"netlist.json",
			"build/netlist.json"
		};
		for (const auto &cand : candidates) {
			if (std::filesystem::exists(cand)) {
				std::string clean_cand;
				std::string sec_err;
				if (ctx.fs_security.validate_access(cand, agentlib::access_type::read, clean_cand, sec_err)) {
					return clean_cand;
				}
			}
		}
		return "";
	};

	if (safe_netlist.empty()) {
		safe_netlist = discover_netlist();
	}

	if (safe_netlist.empty()) {
		set_failure(ctx);
		return "Error: No Yosys netlist JSON found. Please specify 'netlist' path (generate via: "
		       "yosys -p \"read_verilog -sv <files>; hierarchy -check -auto-top; prep -flatten; write_json netlist.json\").";
	}

	verilog::yosys_netlist_reader reader;
	std::string err;
	if (!reader.load_from_file(safe_netlist, err)) {
		set_failure(ctx);
		return std::format("Error loading netlist '{}': {}", safe_netlist, err);
	}

	auto trace = reader.trace_signal(args_.signal);
	std::string fallback_note;

	// If signal was not found and user explicitly passed a netlist:
	if (!trace.has_value() && !args_.safe_netlist.empty()) {
		std::string auto_netlist = discover_netlist();
		if (!auto_netlist.empty() && auto_netlist != safe_netlist) {
			verilog::yosys_netlist_reader fallback_reader;
			std::string fb_err;
			if (fallback_reader.load_from_file(auto_netlist, fb_err)) {
				auto fb_trace = fallback_reader.trace_signal(args_.signal);
				if (fb_trace.has_value()) {
					trace = fb_trace;
					fallback_note = std::format(
						"> [!NOTE] Signal `{}` was not found in `{}` (which appears to be a flattened or "
						"post-synthesis netlist without hierarchy). Automatically switched to hierarchical elaboration netlist `{}`.\n\n",
						args_.signal, args_.requested_netlist, auto_netlist
					);
				}
			}
		}
	}

	if (!trace.has_value()) {
		set_failure(ctx);
		if (reader.is_flat_gate_level()) {
			return std::format(
				"Signal '{}' not found in netlist '{}' (netlist contains {} nets, but appears to be post-synthesis / gate-level without hierarchical names).\n"
				"vcd_signal_to_code expects an RTL elaboration netlist (generate via: yosys -p \"read_verilog -sv <files>; hierarchy -check -auto-top; prep -flatten; write_json netlist.json\").",
				args_.signal, safe_netlist, reader.net_count()
			);
		}
		return std::format("Signal '{}' not found in netlist '{}' (netlist contains {} nets).",
		                   args_.signal, safe_netlist, reader.net_count());
	}

	set_success(ctx);
	return fallback_note + verilog::yosys_netlist_reader::format_trace_summary(*trace);
}

} // namespace tools
