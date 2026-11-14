#include "vcd_search_tool.h"
#include "vcd_parser.h"
#include "vcd_parser_cache.h"
#include "vcd_label_manager.h"
#include "plugins/verilog/fpga_project_settings.h"
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
	std::string parse_err;
	auto parser_ptr = verilog::vcd_parser_cache::get_instance().get_or_parse(args_.safe_path, parse_err);
	if (!parser_ptr) {
		set_failure(ctx, parse_err);
		return std::format("Error parsing VCD file: {}", parse_err);
	}
	const auto &parser = *parser_ptr;

	std::string clock_name = args_.clock_signal;
	if (clock_name.empty()) {
		auto detected = parser.detect_clock();
		if (!detected) {
			set_failure(ctx, "No clock signal specified and failed to auto-detect clock");
			return "Error: No clock signal specified and failed to auto-detect a clock candidate in VCD.";
		}
		clock_name = detected->signal_name;
	}

	struct condition_resolved {
		std::string query_signal;
		std::string query_value;
		const verilog::vcd_signal *sig_info{nullptr};
	};

	std::vector<condition_resolved> resolved_conditions;
	resolved_conditions.reserve(args_.conditions.size());

	std::vector<std::string> sample_signals;

	for (const auto &cond : args_.conditions) {
		std::string_view lookup_name = cond.signal;
		if (lookup_name.starts_with("x:") || lookup_name.starts_with("b:")) {
			lookup_name.remove_prefix(2);
		}
		const auto *target_sig = parser.find_signal(lookup_name);
		if (!target_sig) {
			set_failure(ctx, std::format("Signal '{}' not found in VCD file", cond.signal));
			if (verilog::fpga_project_settings::is_cli_mode()) {
				return std::format("Error: Signal '{}' not found in `{}`. Run `turbostar-verilog vcd-info {}` to confirm available signal names.",
				                   cond.signal, args_.requested_path, args_.requested_path);
			}
			return std::format("Error: Signal '{}' not found in `{}`. Check `vcd_file_info` to confirm available signal names.",
			                   cond.signal, args_.requested_path);
		}
		resolved_conditions.push_back({cond.signal, cond.value, target_sig});
		if (std::ranges::find(sample_signals, cond.signal) == sample_signals.end()) {
			sample_signals.push_back(cond.signal);
		}
	}

	std::string sample_err;
	auto samples = parser.sample_wave(
		clock_name,
		args_.edge,
		sample_signals,
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
		std::string sample_values_desc;
	};

	std::vector<matched_interval> all_intervals;
	bool in_interval = false;
	matched_interval cur_interval;

	for (const auto &sample : samples) {
		bool is_match = true;
		std::vector<std::string> match_pieces;

		for (const auto &cond : resolved_conditions) {
			auto it_val = sample.signal_values.find(cond.query_signal);
			std::string hex_val = (it_val != sample.signal_values.end()) ? it_val->second : "";
			if (hex_val == "-") {
				is_match = false;
				break;
			}
			auto it_raw = sample.signal_raw_values.find(cond.query_signal);
			std::string raw_bin = (it_raw != sample.signal_raw_values.end()) ? it_raw->second : "";

			std::string label = label_mgr.find_label(args_.safe_path, cond.sig_info->full_name, hex_val, raw_bin);
			if (!verilog::vcd_label_manager::value_matches(cond.query_value, hex_val, raw_bin, label)) {
				is_match = false;
				break;
			}

			if (resolved_conditions.size() == 1) {
				std::string piece = hex_val;
				if (!label.empty()) {
					piece = std::format("{} ({})", hex_val, label);
				}
				match_pieces.push_back(std::move(piece));
			} else {
				std::string piece = std::format("{}={}", cond.query_signal, hex_val);
				if (!label.empty()) {
					piece += std::format(" ({})", label);
				}
				match_pieces.push_back(std::move(piece));
			}
		}

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
				std::string desc;
				for (size_t i = 0; i < match_pieces.size(); ++i) {
					if (i > 0) desc += ", ";
					desc += match_pieces[i];
				}
				cur_interval.sample_values_desc = std::move(desc);
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
	std::string cond_summary;
	for (size_t i = 0; i < resolved_conditions.size(); ++i) {
		if (i > 0) cond_summary += " AND ";
		if (resolved_conditions.size() == 1) {
			cond_summary += std::format("`{}` == `{}`",
			                            resolved_conditions[i].sig_info->full_name,
			                            resolved_conditions[i].query_value);
		} else {
			cond_summary += std::format("`{}` == `{}`",
			                            resolved_conditions[i].query_signal,
			                            resolved_conditions[i].query_value);
		}
	}

	ss << verilog::vcd_parser_cache::check_stale_rtl_warning(args_.safe_path, args_.requested_path);
	ss << std::format("### VCD Search: {} in `{}`\n\n", cond_summary, args_.requested_path);
	if (all_intervals.empty()) {
		ss << std::format("*No clock cycles matched {} in the specified window.*\n", cond_summary);
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
	std::string col_name = (resolved_conditions.size() > 1) ? "Matched Values" : "Matched Value";
	ss << "| Interval | Start Tick | End Tick | Cycles | Time Window (" << parser.get_timescale() << ") | " << col_name << " |\n";
	ss << "|:---------|:-----------|:---------|:-------|:-------------------|:" << std::string(col_name.size() + 2, '-') << "|\n";

	for (size_t i = 0; i < shown_count; ++i) {
		const auto &inv = all_intervals[i];
		std::string time_window_str;
		if (inv.start_time == inv.end_time) {
			time_window_str = std::format("{}", inv.start_time);
		} else {
			time_window_str = std::format("{} – {}", inv.start_time, inv.end_time);
		}

		ss << std::format("| {} | {} | {} | {} | {} | {} |\n",
		                  i + 1, inv.start_tick, inv.end_tick, inv.cycle_count,
		                  time_window_str, inv.sample_values_desc);
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
		if (verilog::fpga_project_settings::is_cli_mode()) {
			std::string cli_sigs;
			for (size_t i = 0; i < sample_signals.size(); ++i) {
				if (i > 0) cli_sigs += ",";
				cli_sigs += sample_signals[i];
			}
			ss << std::format("\n> [!TIP] To inspect waveforms around match 1, run:\n"
			                  "> `turbostar-verilog vcd-wave {} -s {} --start {} --end {}`\n",
			                  args_.requested_path, cli_sigs, wave_start, wave_end);
		} else {
			std::string sig_list_str;
			for (size_t i = 0; i < sample_signals.size(); ++i) {
				if (i > 0) sig_list_str += ", ";
				sig_list_str += std::format("\"{}\"", sample_signals[i]);
			}
			ss << std::format("\n> [!TIP] To inspect waveforms around match 1, run:\n"
			                  "> `vcd_wave(path=\"{}\", signals=[{}], start_tick={}, end_tick={})`\n",
			                  args_.requested_path, sig_list_str, wave_start, wave_end);
		}
	}

	set_success(ctx);
	return ss.str();
}

} // namespace tools
