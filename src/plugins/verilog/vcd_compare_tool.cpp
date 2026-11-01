#include "vcd_compare_tool.h"
#include "vcd_parser.h"
#include "vcd_label_manager.h"
#include <format>
#include <sstream>
#include <algorithm>
#include <set>

namespace tools
{

vcd_compare_tool::vcd_compare_tool(vcd_compare_args args)
	: agentlib::llm_tool_action("Comparing VCD simulation traces"), args_(std::move(args))
{
}

bool vcd_compare_tool::validate_runtime(const agentlib::tool_context &/*ctx*/, std::string &/*out_error*/) const
{
	return true;
}

std::string vcd_compare_tool::execute(agentlib::tool_context &ctx)
{
	verilog::vcd_parser parser_a;
	std::string err_a;
	if (!parser_a.parse_all(args_.safe_path_a, err_a)) {
		set_failure(ctx, err_a);
		return std::format("Error parsing File A (`{}`): {}", args_.requested_path_a, err_a);
	}

	verilog::vcd_parser parser_b;
	std::string err_b;
	if (!parser_b.parse_all(args_.safe_path_b, err_b)) {
		set_failure(ctx, err_b);
		return std::format("Error parsing File B (`{}`): {}", args_.requested_path_b, err_b);
	}

	std::string clock_name = args_.clock_signal;
	if (clock_name.empty()) {
		auto detected_a = parser_a.detect_clock();
		if (detected_a) {
			clock_name = detected_a->signal_name;
		} else {
			auto detected_b = parser_b.detect_clock();
			if (detected_b) {
				clock_name = detected_b->signal_name;
			} else {
				set_failure(ctx, "Failed to auto-detect reference clock signal in traces");
				return "Error: No clock signal specified and failed to auto-detect clock in either VCD trace.";
			}
		}
	}

	if (!parser_a.find_signal(clock_name)) {
		set_failure(ctx, std::format("Clock signal '{}' not found in File A", clock_name));
		return std::format("Error: Clock signal '{}' not found in File A (`{}`).", clock_name, args_.requested_path_a);
	}
	if (!parser_b.find_signal(clock_name)) {
		set_failure(ctx, std::format("Clock signal '{}' not found in File B", clock_name));
		return std::format("Error: Clock signal '{}' not found in File B (`{}`).", clock_name, args_.requested_path_b);
	}

	// Filter and build candidate signal lists from both parsers
	auto get_filtered_signals = [this](const verilog::vcd_parser &parser) -> std::set<std::string> {
		std::set<std::string> out;
		for (const auto &sig : parser.get_signals()) {
			// Skip clock signal itself from data comparison
			if (sig.full_name == args_.clock_signal) {
				continue;
			}

			// Scope filter
			if (!args_.scope.empty()) {
				if (sig.scope != args_.scope && !sig.scope.starts_with(args_.scope + ".")) {
					continue;
				}
			}

			// Signals / wildcard filter
			if (!args_.signals.empty()) {
				bool matched = false;
				for (const auto &pattern : args_.signals) {
					if (pattern == sig.full_name || pattern == sig.short_name ||
					    verilog::vcd_label_manager::signal_pattern_matches(pattern, sig.full_name)) {
						matched = true;
						break;
					}
				}
				if (!matched) {
					continue;
				}
			}

			out.insert(sig.full_name);
		}
		return out;
	};

	std::set<std::string> sigs_a = get_filtered_signals(parser_a);
	std::set<std::string> sigs_b = get_filtered_signals(parser_b);

	std::vector<std::string> common_signals;
	std::vector<std::string> unique_a;
	std::vector<std::string> unique_b;

	for (const auto &s : sigs_a) {
		if (sigs_b.contains(s)) {
			common_signals.push_back(s);
		} else {
			unique_a.push_back(s);
		}
	}
	for (const auto &s : sigs_b) {
		if (!sigs_a.contains(s)) {
			unique_b.push_back(s);
		}
	}

	if (args_.strict && (!unique_a.empty() || !unique_b.empty())) {
		set_failure(ctx, "Strict signal manifest check failed");
		std::ostringstream ss;
		ss << std::format("### VCD Compare: Signal Set Mismatch (Strict Mode)\n\n"
		                  "Strict comparison failed: File A and File B do not have identical signal manifests.\n\n");
		if (!unique_a.empty()) {
			ss << std::format("- **Signals in File A only ({})**:\n", unique_a.size());
			for (size_t i = 0; i < std::min(unique_a.size(), size_t(10)); ++i) {
				ss << std::format("  - `{}`\n", unique_a[i]);
			}
			if (unique_a.size() > 10) {
				ss << std::format("  - *(and {} more)*\n", unique_a.size() - 10);
			}
		}
		if (!unique_b.empty()) {
			ss << std::format("- **Signals in File B only ({})**:\n", unique_b.size());
			for (size_t i = 0; i < std::min(unique_b.size(), size_t(10)); ++i) {
				ss << std::format("  - `{}`\n", unique_b[i]);
			}
			if (unique_b.size() > 10) {
				ss << std::format("  - *(and {} more)*\n", unique_b.size() - 10);
			}
		}
		ss << std::format("\n- **Common signals ({})**\n", common_signals.size());
		ss << "\n*(Tip: Run with `strict: false` to compare the common signal intersection.)*\n";
		return ss.str();
	}

	if (common_signals.empty()) {
		set_failure(ctx, "No common signals found to compare");
		return std::format("Error: No common signals found between `{}` and `{}` matching filter criteria.",
		                   args_.requested_path_a, args_.requested_path_b);
	}

	auto &label_mgr = verilog::vcd_label_manager::get_instance();

	// Inherit qualifiers from File A to File B if File B doesn't have qualifiers for common signals
	auto quals_a = label_mgr.get_qualifiers_for_file(args_.safe_path_a);
	auto quals_b = label_mgr.get_qualifiers_for_file(args_.safe_path_b);
	if (!quals_a.empty() && quals_b.empty()) {
		bool saved = false;
		std::string msg;
		label_mgr.set_qualifiers_batch(args_.safe_path_b, quals_a, saved, msg);
	}

	std::string sample_err_a;
	auto samples_a = parser_a.sample_wave(
		clock_name,
		args_.edge,
		common_signals,
		args_.start_tick,
		args_.end_tick,
		args_.start_time,
		args_.end_time,
		0,
		sample_err_a,
		false
	);
	if (!sample_err_a.empty()) {
		set_failure(ctx, sample_err_a);
		return std::format("Error sampling File A: {}", sample_err_a);
	}

	std::string sample_err_b;
	auto samples_b = parser_b.sample_wave(
		clock_name,
		args_.edge,
		common_signals,
		args_.start_tick,
		args_.end_tick,
		args_.start_time,
		args_.end_time,
		0,
		sample_err_b,
		false
	);
	if (!sample_err_b.empty()) {
		set_failure(ctx, sample_err_b);
		return std::format("Error sampling File B: {}", sample_err_b);
	}

	struct diff_info {
		std::string signal;
		std::string val_a;
		std::string val_b;
		std::string desc;
	};

	struct divergent_cycle {
		size_t tick{0};
		uint64_t timestamp_a{0};
		uint64_t timestamp_b{0};
		std::vector<diff_info> diffs;
	};

	std::vector<divergent_cycle> divergence_events;
	size_t compared_cycles = std::min(samples_a.size(), samples_b.size());

	for (size_t i = 0; i < compared_cycles; ++i) {
		const auto &sa = samples_a[i];
		const auto &sb = samples_b[i];

		std::vector<diff_info> tick_diffs;

		for (const auto &sig : common_signals) {
			auto it_a = sa.signal_values.find(sig);
			auto it_b = sb.signal_values.find(sig);
			std::string va = (it_a != sa.signal_values.end()) ? it_a->second : "";
			std::string vb = (it_b != sb.signal_values.end()) ? it_b->second : "";

			if (args_.ignore_invalid) {
				if (va == "-" && vb == "-") {
					continue;
				}
			}

			if (va != vb) {
				diff_info d;
				d.signal = sig;

				auto it_raw_a = sa.signal_raw_values.find(sig);
				auto it_raw_b = sb.signal_raw_values.find(sig);
				std::string raw_a = (it_raw_a != sa.signal_raw_values.end()) ? it_raw_a->second : "";
				std::string raw_b = (it_raw_b != sb.signal_raw_values.end()) ? it_raw_b->second : "";

				std::string lbl_a = label_mgr.find_label(args_.safe_path_a, sig, va, raw_a);
				std::string lbl_b = label_mgr.find_label(args_.safe_path_b, sig, vb, raw_b);

				d.val_a = va;
				if (!lbl_a.empty()) d.val_a += std::format(" ({})", lbl_a);
				if (va == "-") d.val_a = "- (invalid)";

				d.val_b = vb;
				if (!lbl_b.empty()) d.val_b += std::format(" ({})", lbl_b);
				if (vb == "-") d.val_b = "- (invalid)";

				if (va == "-" || vb == "-") {
					d.desc = "Validity mismatch";
				} else {
					d.desc = "Value mismatch";
				}

				tick_diffs.push_back(std::move(d));
			}
		}

		if (!tick_diffs.empty()) {
			divergence_events.push_back(divergent_cycle{
				.tick = sa.tick,
				.timestamp_a = sa.timestamp,
				.timestamp_b = sb.timestamp,
				.diffs = std::move(tick_diffs)
			});
		}
	}

	std::ostringstream ss;

	if (divergence_events.empty()) {
		if (samples_a.size() != samples_b.size()) {
			ss << "### VCD Compare: MATCH (Trace Length Mismatch)\n\n";
			ss << std::format("- **File A (Reference)**: `{}` ({} cycles)\n", args_.requested_path_a, samples_a.size());
			ss << std::format("- **File B (Candidate)**: `{}` ({} cycles)\n", args_.requested_path_b, samples_b.size());
			ss << std::format("- **Reference Clock**: `{}` (sampled on `{}`)\n", clock_name, args_.edge);
			ss << std::format("- **Compared Window**: Ticks 0–{} matched identically across {} common signals.\n",
			                  compared_cycles > 0 ? compared_cycles - 1 : 0, common_signals.size());
			ss << std::format("*(Note: Trace `{}` ended earlier than `{}`.)*\n\n",
			                  samples_a.size() < samples_b.size() ? args_.requested_path_a : args_.requested_path_b,
			                  samples_a.size() < samples_b.size() ? args_.requested_path_b : args_.requested_path_a);
		} else {
			ss << "### VCD Compare: MATCH\n\n";
			ss << std::format("- **File A (Reference)**: `{}`\n", args_.requested_path_a);
			ss << std::format("- **File B (Candidate)**: `{}`\n", args_.requested_path_b);
			ss << std::format("- **Reference Clock**: `{}` (sampled on `{}`)\n", clock_name, args_.edge);
			ss << std::format("- **Evaluated Window**: {} cycles (ticks 0–{})\n",
			                  compared_cycles, compared_cycles > 0 ? compared_cycles - 1 : 0);
			ss << std::format("- **Signals Compared**: {} common signals (0 differences detected)\n\n",
			                  common_signals.size());
		}

		if (!unique_a.empty() || !unique_b.empty()) {
			ss << "> [!NOTE] Non-matching signal manifests (ignored during comparison):\n";
			if (!unique_a.empty()) {
				ss << std::format("> - **Unique to File A ({})**: ", unique_a.size());
				for (size_t i = 0; i < std::min(unique_a.size(), size_t(5)); ++i) {
					if (i > 0) ss << ", ";
					ss << std::format("`{}`", unique_a[i]);
				}
				if (unique_a.size() > 5) ss << std::format(", *(and {} more)*", unique_a.size() - 5);
				ss << "\n";
			}
			if (!unique_b.empty()) {
				ss << std::format("> - **Unique to File B ({})**: ", unique_b.size());
				for (size_t i = 0; i < std::min(unique_b.size(), size_t(5)); ++i) {
					if (i > 0) ss << ", ";
					ss << std::format("`{}`", unique_b[i]);
				}
				if (unique_b.size() > 5) ss << std::format(", *(and {} more)*", unique_b.size() - 5);
				ss << "\n";
			}
		}

		set_success(ctx);
		return ss.str();
	}

	// Divergence detected
	const auto &first_div = divergence_events.front();

	ss << std::format("### VCD Compare: DIVERGENCE DETECTED at Tick {} (Time {}{})\n\n",
	                  first_div.tick, first_div.timestamp_a, parser_a.get_timescale());
	ss << std::format("- **File A (Reference)**: `{}`\n", args_.requested_path_a);
	ss << std::format("- **File B (Candidate)**: `{}`\n", args_.requested_path_b);
	ss << std::format("- **Reference Clock**: `{}` (sampled on `{}`)\n", clock_name, args_.edge);
	ss << std::format("- **Evaluated Window**: {} cycles (divergence at tick {}; {} of {} cycles differed)\n",
	                  compared_cycles, first_div.tick, divergence_events.size(), compared_cycles);
	ss << std::format("- **Signals Compared**: {} common signals\n\n", common_signals.size());

	if (!unique_a.empty() || !unique_b.empty()) {
		ss << std::format("> [!NOTE] {} signal(s) unique to File A and {} unique to File B were omitted from comparison.\n\n",
		                  unique_a.size(), unique_b.size());
	}

	ss << std::format("#### Divergent Signals at First Divergence (Tick {}):\n", first_div.tick);
	ss << "| Signal | File A (Reference) | File B (Candidate) | Difference |\n";
	ss << "|:-------|:-------------------|:-------------------|:-----------|\n";

	size_t show_diff_count = std::min(first_div.diffs.size(), size_t(20));
	std::vector<std::string> divergent_sig_names;
	for (size_t i = 0; i < show_diff_count; ++i) {
		const auto &d = first_div.diffs[i];
		ss << std::format("| `{}` | `{}` | `{}` | {} |\n", d.signal, d.val_a, d.val_b, d.desc);
		if (divergent_sig_names.size() < 4) {
			divergent_sig_names.push_back(d.signal);
		}
	}

	if (first_div.diffs.size() > show_diff_count) {
		ss << std::format("\n*(Showing first {} of {} differing signals at this tick.)*\n",
		                  show_diff_count, first_div.diffs.size());
	}

	// Actionable wave inspection tip
	size_t wave_start = (first_div.tick > 2) ? (first_div.tick - 2) : 0;
	size_t wave_end = first_div.tick + 3;
	std::string sig_str;
	for (size_t i = 0; i < divergent_sig_names.size(); ++i) {
		if (i > 0) sig_str += ", ";
		sig_str += std::format("\"{}\"", divergent_sig_names[i]);
	}

	ss << std::format("\n> [!TIP] To inspect waveforms around divergence in File B (ticks {}–{}), run:\n"
	                  "> `vcd_wave(path=\"{}\", signals=[{}], start_tick={}, end_tick={})`\n",
	                  wave_start, wave_end, args_.requested_path_b, sig_str, wave_start, wave_end);

	set_success(ctx);
	return ss.str();
}

} // namespace tools
