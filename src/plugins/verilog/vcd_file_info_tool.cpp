#include "vcd_file_info_tool.h"
#include "vcd_parser.h"
#include "vcd_label_manager.h"
#include <format>
#include <sstream>

namespace tools
{

vcd_file_info_tool::vcd_file_info_tool(vcd_file_info_args args)
	: agentlib::llm_tool_action("Inspecting VCD file"), args_(std::move(args))
{
}

bool vcd_file_info_tool::validate_runtime(const agentlib::tool_context &/*ctx*/, std::string &/*out_error*/) const
{
	return true;
}

std::string vcd_file_info_tool::execute(agentlib::tool_context &ctx)
{
	verilog::vcd_parser parser;
	std::string parse_err;

	// Parse full simulation to get clock analysis and transition counts
	if (!parser.parse_all(args_.safe_path, parse_err)) {
		set_failure(ctx, parse_err);
		return std::format("Error parsing VCD file: {}", parse_err);
	}

	std::ostringstream ss;
	ss << std::format("### VCD File Information: `{}`\n\n", args_.requested_path);
	if (!parser.get_version().empty()) {
		ss << std::format("- **Generator / Version**: {}\n", parser.get_version());
	}
	if (!parser.get_date().empty()) {
		ss << std::format("- **Date**: {}\n", parser.get_date());
	}
	ss << std::format("- **Timescale**: {}\n", parser.get_timescale());
	ss << std::format("- **Time Range**: {} to {} {} (Duration: {} {})\n",
		parser.get_start_time(), parser.get_end_time(), parser.get_timescale(),
		parser.get_end_time() - parser.get_start_time(), parser.get_timescale());

	// Clock analysis
	std::optional<verilog::vcd_clock_info> clk_info;
	if (!args_.clock_signal.empty()) {
		clk_info = parser.analyze_clock(args_.clock_signal);
	} else {
		clk_info = parser.detect_clock();
	}

	if (clk_info) {
		ss << std::format("- **Reference Clock**: `{}`\n", clk_info->signal_name);
		if (clk_info->period > 0) {
			ss << std::format("  - **Period**: {} {}\n", clk_info->period, parser.get_timescale());
		}
		ss << std::format("  - **Clock Ticks**: {} posedges, {} negedges\n",
			clk_info->posedge_count, clk_info->negedge_count);
	}

	const auto &all_signals = parser.get_signals();
	ss << std::format("- **Total Signals**: {}\n", all_signals.size());

	// Active signal labels
	auto &label_mgr = verilog::vcd_label_manager::get_instance();
	auto active_labels = label_mgr.get_labels_for_file(args_.safe_path);
	if (!active_labels.empty()) {
		ss << "- **Active Signal Labels**: ";
		bool first = true;
		for (const auto &[sig, val_map] : active_labels) {
			if (!first) ss << ", ";
			ss << std::format("`{}` ({} labels)", sig, val_map.size());
			first = false;
		}
		ss << "\n";
	}
	ss << "\n";

	// Filter signals
	auto filtered = parser.filter_signals(args_.scope, args_.filter);
	if (!args_.scope.empty() || !args_.filter.empty()) {
		ss << std::format("#### Filtered Signals (Scope: `{}` | Filter: `{}` | Count: {})\n\n",
			args_.scope.empty() ? "*" : args_.scope,
			args_.filter.empty() ? "*" : args_.filter,
			filtered.size());
	} else {
		ss << "#### Signals\n\n";
	}

	ss << "| Signal Name | Width | Type | Slice |\n";
	ss << "|:------------|:-----:|:----:|:-----:|\n";

	size_t displayed = 0;
	for (const auto *sig : filtered) {
		if (displayed >= args_.max_signals) {
			ss << std::format("\n*... [{} additional signals omitted. Use 'scope', 'filter', or increase 'max_signals']*\n",
				filtered.size() - displayed);
			break;
		}
		ss << std::format("| `{}` | {} | {} | {} |\n",
			sig->full_name, sig->bit_width, sig->var_type,
			sig->slice.empty() ? "-" : sig->slice);
		++displayed;
	}

	set_success(ctx);
	return ss.str();
}

} // namespace tools
