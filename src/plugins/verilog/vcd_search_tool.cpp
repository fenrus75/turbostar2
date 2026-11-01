#include "vcd_search_tool.h"
#include "vcd_parser.h"
#include "vcd_label_manager.h"
#include <format>
#include <sstream>
#include <algorithm>

namespace tools
{

vcd_search_tool::vcd_search_tool(vcd_search_args args)
	: agentlib::llm_tool_action("Searching VCD waveform"), args_(std::move(args))
{
}

bool vcd_search_tool::validate_runtime(const agentlib::tool_context &/*ctx*/, std::string &/*out_error*/) const
{
	return true;
}

std::string vcd_search_tool::execute(agentlib::tool_context &ctx)
{
	verilog::vcd_parser parser;
	std::string parse_err;

	if (!parser.parse_all(args_.safe_path, parse_err)) {
		set_failure(ctx, parse_err);
		return std::format("Error parsing VCD file: {}", parse_err);
	}

	std::string clock_name = args_.clock_signal;
	if (clock_name.empty()) {
		auto detected = parser.detect_clock();
		if (!detected) {
			set_failure(ctx, "No clock signal specified and failed to auto-detect clock");
			return "Error: No clock signal specified and failed to auto-detect a clock candidate in VCD.";
		}
		clock_name = detected->signal_name;
	}

	std::string_view lookup_name = args_.signal;
	if (lookup_name.starts_with("x:") || lookup_name.starts_with("b:")) {
		lookup_name.remove_prefix(2);
	}
	const auto *target_sig = parser.find_signal(lookup_name);
	if (!target_sig) {
		set_failure(ctx, std::format("Signal '{}' not found in VCD file", args_.signal));
		return std::format("Error: Signal '{}' not found in `{}`. Check `vcd_file_info` to confirm available signal names.",
		                   args_.signal, args_.requested_path);
	}

	std::string sample_err;
	auto samples = parser.sample_wave(
		clock_name,
		args_.edge,
		{args_.signal},
		args_.start_tick,
		args_.end_tick,
		args_.start_time,
		args_.end_time,
		0, // unlimited rows
		sample_err,
		false // collapse_idle = false
	);

	if (!sample_err.empty()) {
		set_failure(ctx, sample_err);
		return std::format("Error sampling waveform: {}", sample_err);
	}

	auto &label_mgr = verilog::vcd_label_manager::get_instance();

	struct matched_interval {
		size_t start_tick{0};
		size_t end_tick{0};
		uint64_t start_time{0};
		uint64_t end_time{0};
		size_t cycle_count{0};
		std::string sample_val;
		std::string sample_label;
	};

	std::vector<matched_interval> all_intervals;
	bool in_interval = false;
	matched_interval cur_interval;

	for (const auto &sample : samples) {
		auto it_val = sample.signal_values.find(args_.signal);
		std::string hex_val = (it_val != sample.signal_values.end()) ? it_val->second : "";
		auto it_raw = sample.signal_raw_values.find(args_.signal);
		std::string raw_bin = (it_raw != sample.signal_raw_values.end()) ? it_raw->second : "";

		std::string label = label_mgr.find_label(args_.safe_path, target_sig->full_name, hex_val, raw_bin);
		bool is_match = verilog::vcd_label_manager::value_matches(args_.value, hex_val, raw_bin, label);

		if (is_match) {
			if (in_interval && sample.tick == cur_interval.end_tick + 1) {
				cur_interval.end_tick = sample.tick;
				cur_interval.end_time = sample.timestamp;
				cur_interval.cycle_count++;
			} else {
				if (in_interval) {
					all_intervals.push_back(cur_interval);
				}
				cur_interval.start_tick = sample.tick;
				cur_interval.end_tick = sample.tick;
				cur_interval.start_time = sample.timestamp;
				cur_interval.end_time = sample.timestamp;
				cur_interval.cycle_count = 1;
				cur_interval.sample_val = hex_val;
				cur_interval.sample_label = label;
				in_interval = true;
			}
		} else {
			if (in_interval) {
				all_intervals.push_back(cur_interval);
				in_interval = false;
			}
		}
	}

	if (in_interval) {
		all_intervals.push_back(cur_interval);
	}

	std::ostringstream ss;
	ss << std::format("### VCD Search: `{}` == `{}` in `{}`\n\n",
	                  target_sig->full_name, args_.value, args_.requested_path);

	if (all_intervals.empty()) {
		ss << std::format("*No clock cycles matched signal `{}` == `{}` in the specified window.*\n",
		                  target_sig->full_name, args_.value);
		set_success(ctx);
		return ss.str();
	}

	size_t total_matched_cycles = 0;
	for (const auto &inv : all_intervals) {
		total_matched_cycles += inv.cycle_count;
	}

	// Direction handling
	bool backward = (args_.direction == "backward");
	if (backward) {
		std::ranges::reverse(all_intervals);
	}

	size_t shown_count = std::min(all_intervals.size(), args_.limit);

	// Build tick ranges summary string for shown intervals
	std::vector<std::string> range_strs;
	range_strs.reserve(shown_count);
	for (size_t i = 0; i < shown_count; ++i) {
		const auto &inv = all_intervals[i];
		if (inv.start_tick == inv.end_tick) {
			range_strs.push_back(std::format("{}", inv.start_tick));
		} else {
			range_strs.push_back(std::format("{}-{}", inv.start_tick, inv.end_tick));
		}
	}

	std::string ranges_summary;
	for (size_t i = 0; i < range_strs.size(); ++i) {
		if (i > 0) ranges_summary += ", ";
		ranges_summary += range_strs[i];
	}

	ss << std::format("**Matched {} interval(s)** (total {} cycles, sampled on `{}` {}):\n",
	                  all_intervals.size(), total_matched_cycles, clock_name, args_.edge);
	ss << std::format("* **Tick ranges**: `{}`\n\n", ranges_summary);

	// Table breakdown
	ss << "| Interval | Start Tick | End Tick | Cycles | Time Window (" << parser.get_timescale() << ") | Matched Value |\n";
	ss << "|:---------|:-----------|:---------|:-------|:-------------------|:--------------|\n";

	for (size_t i = 0; i < shown_count; ++i) {
		const auto &inv = all_intervals[i];
		std::string val_str = inv.sample_val;
		if (!inv.sample_label.empty()) {
			val_str = std::format("{} ({})", val_str, inv.sample_label);
		}

		std::string time_window_str;
		if (inv.start_time == inv.end_time) {
			time_window_str = std::format("{}", inv.start_time);
		} else {
			time_window_str = std::format("{} – {}", inv.start_time, inv.end_time);
		}

		ss << std::format("| {} | {} | {} | {} | {} | {} |\n",
		                  i + 1, inv.start_tick, inv.end_tick, inv.cycle_count,
		                  time_window_str, val_str);
	}

	if (all_intervals.size() > shown_count) {
		ss << std::format("\n*(Showing first {} of {} matched intervals. Use `limit` to see more.)*\n",
		                  shown_count, all_intervals.size());
	}

	// Helpful follow-up wave hint
	if (!all_intervals.empty()) {
		const auto &first_inv = all_intervals.front();
		size_t wave_start = (first_inv.start_tick > 1) ? (first_inv.start_tick - 1) : 0;
		size_t wave_end = first_inv.end_tick + 1;
		ss << std::format("\n> [!TIP] To inspect waveforms around match 1, run:\n"
		                  "> `vcd_wave(path=\"{}\", signals=[\"{}\"], start_tick={}, end_tick={})`\n",
		                  args_.requested_path, args_.signal, wave_start, wave_end);
	}

	set_success(ctx);
	return ss.str();
}

} // namespace tools
