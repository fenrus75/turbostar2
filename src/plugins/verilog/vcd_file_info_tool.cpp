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

	const verilog::vcd_signal *clk_sig = nullptr;
	std::string clk_name(args_.clock_signal);
	if (clk_name.empty() && clk_info.has_value()) {
		clk_name = clk_info->signal_name;
	}
	if (!clk_name.empty()) {
		clk_sig = parser.find_signal(clk_name);
	}

	if (clk_info) {
		ss << std::format("- **Reference Clock**: `{}`\n", clk_info->signal_name);
		if (clk_info->period > 0) {
			ss << std::format("  - **Period**: {} {}\n", clk_info->period, parser.get_timescale());
		}
		ss << std::format("  - **Clock Ticks**: {} posedges, {} negedges\n",
			clk_info->posedge_count, clk_info->negedge_count);
	}

	if (args_.stats && (args_.start_tick || args_.end_tick)) {
		ss << std::format("- **Analysis Window**: Ticks {} to {}\n",
			args_.start_tick.value_or(0),
			args_.end_tick ? std::to_string(*args_.end_tick) : "end");
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

	bool has_clock = clk_info.has_value();

	if (args_.stats) {
		if (has_clock) {
			ss << "| Signal Name | Width | Type | First Toggle (Tick) | Toggle Rate |\n";
			ss << "|:------------|:-----:|:----:|:-------------------:|:-----------:|\n";
		} else {
			ss << "| Signal Name | Width | Type | First Toggle (Time) | Toggle Rate |\n";
			ss << "|:------------|:-----:|:----:|:-------------------:|:-----------:|\n";
		}
	} else {
		ss << "| Signal Name | Width | Type | Slice |\n";
		ss << "|:------------|:-----:|:----:|:-----:|\n";
	}

	size_t displayed = 0;
	size_t never_toggled_count = 0;

	for (const auto *sig : filtered) {
		if (displayed >= args_.max_signals) {
			ss << std::format("\n*... [{} additional signals omitted. Use 'scope', 'filter', or increase 'max_signals']*\n",
				filtered.size() - displayed);
			break;
		}

		bool is_ref_clk = clk_sig && (sig->id_code == clk_sig->id_code || sig->full_name == clk_sig->full_name);

		auto stats = parser.compute_signal_stats(*sig, args_.clock_signal, args_.start_tick, args_.end_tick);
		bool never_toggles = !stats.has_transitions && !is_ref_clk;
		if (never_toggles) {
			never_toggled_count++;
		}

		std::string name_col = never_toggles ? std::format("`{}`**", sig->full_name) : std::format("`{}`", sig->full_name);

		if (args_.stats) {
			std::string first_toggle_str;
			if (is_ref_clk) {
				first_toggle_str = "0";
			} else if (!stats.has_transitions) {
				first_toggle_str = "never";
			} else if (has_clock && stats.first_toggle_tick.has_value()) {
				first_toggle_str = std::format("{}", *stats.first_toggle_tick);
			} else if (stats.first_toggle_time.has_value()) {
				first_toggle_str = std::format("{}", *stats.first_toggle_time);
			} else {
				first_toggle_str = "async";
			}

			std::string rate_str;
			if (is_ref_clk) {
				rate_str = "[ref clk]";
			} else if (!stats.has_transitions || stats.toggle_count == 0) {
				rate_str = "0%";
			} else {
				rate_str = std::format("{:.1f}%", stats.toggle_rate_percent);
			}

			ss << std::format("| {} | {} | {} | {} | {} |\n",
				name_col, sig->bit_width, sig->var_type, first_toggle_str, rate_str);
		} else {
			ss << std::format("| {} | {} | {} | {} |\n",
				name_col, sig->bit_width, sig->var_type,
				sig->slice.empty() ? "-" : sig->slice);
		}

		++displayed;
	}

	if (args_.stats) {
		ss << "\n> [!NOTE] Toggle Rate measures the percentage of clock cycles where the posedge-sampled signal value changes from the prior cycle";
		if (has_clock && clk_info) {
			ss << std::format(" (reference clock: `{}`)", clk_info->signal_name);
		}
		ss << ".";
		if (args_.start_tick || args_.end_tick) {
			ss << std::format(" Window: ticks {} to {}.",
				args_.start_tick.value_or(0),
				args_.end_tick ? std::to_string(*args_.end_tick) : "end");
		}
		if (never_toggled_count > 0) {
			ss << " Signals marked with `**` had 0 transitions in this range (constant value).";
		}
		ss << "\n";
	} else if (never_toggled_count > 0) {
		ss << "\n> [!NOTE] Signals marked with `**` had 0 transitions throughout the simulation. Pass `stats: true` for full toggle rates.\n";
	}
	set_success(ctx);
	return ss.str();
}

} // namespace tools
