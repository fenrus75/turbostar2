#include "vcd_wave_tool.h"
#include "vcd_parser.h"
#include "vcd_parser_cache.h"
#include "vcd_label_manager.h"
#include <format>
#include <sstream>
#include <fstream>
#include <set>
namespace tools
{

vcd_wave_tool::vcd_wave_tool(vcd_wave_args args)
	: agentlib::llm_tool_action("Sampling VCD waveform"), args_(std::move(args))
{
}

bool vcd_wave_tool::validate_runtime(const agentlib::tool_context &/*ctx*/, std::string &/*out_error*/) const
{
	return true;
}

std::string vcd_wave_tool::execute(agentlib::tool_context &ctx)
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

	std::string sample_err;
	auto samples = parser.sample_wave(
		clock_name,
		args_.edge,
		args_.signals,
		args_.start_tick,
		args_.end_tick,
		args_.start_time,
		args_.end_time,
		args_.max_rows,
		sample_err,
		args_.collapse_idle
	);

	if (!sample_err.empty()) {
		set_failure(ctx, sample_err);
		return std::format("Error sampling waveform: {}", sample_err);
	}

	size_t total_cycles = 0;
	for (const auto &sample : samples) {
		total_cycles += (sample.collapsed_count > 0 ? sample.collapsed_count : 1);
	}

	std::ostringstream ss;
	ss << std::format("### VCD Waveform: `{}`\n", args_.requested_path);
	if (total_cycles != samples.size()) {
		ss << std::format("Sampled on: `{}` ({}) | Timescale: {} | Rows: {} (representing {} cycles)\n\n",
			clock_name, args_.edge, parser.get_timescale(), samples.size(), total_cycles);
	} else {
		ss << std::format("Sampled on: `{}` ({}) | Timescale: {} | Total Rows: {}\n\n",
			clock_name, args_.edge, parser.get_timescale(), samples.size());
	}

	if (samples.empty()) {
		ss << "*No clock transitions matched the requested tick/time window.*\n";
		set_success(ctx);
		return ss.str();
	}

	// Build markdown table header
	ss << "| Tick | Time (" << parser.get_timescale() << ") ";
	if (args_.edge == "both") {
		ss << "| Edge ";
	}
	for (const auto &sig : args_.signals) {
		ss << "| `" << sig << "` ";
	}
	ss << "|\n";

	// Alignments
	ss << "|:-----|:-------------------|";
	if (args_.edge == "both") {
		ss << ":----:|";
	}
	for (size_t i = 0; i < args_.signals.size(); ++i) {
		ss << ":------|";
	}
	ss << "\n";

	// Provenance row: if trace is windowed (start_tick > 0 or start_time > start_time_),
	// show earliest clock tick (or time) each signal has continuously held its starting value.
	bool has_clock = !clock_name.empty();
	bool show_since = false;
	if (!samples.empty()) {
		if (has_clock) {
			show_since = (samples.front().tick > 0);
		} else {
			show_since = (samples.front().timestamp > parser.get_start_time());
		}
	}

	if (show_since && !samples.empty()) {
		const auto &first_sample = samples.front();
		auto edge_times = has_clock ? parser.get_clock_edge_times(clock_name, args_.edge) : std::vector<uint64_t>{};

		ss << std::format("| *({})* | - ", has_clock ? "since clock" : "since time");
		if (args_.edge == "both") {
			ss << "| - ";
		}
		for (const auto &sig : args_.signals) {
			auto val_it = first_sample.signal_values.find(sig);
			if (val_it != first_sample.signal_values.end() && val_it->second == "-") {
				// Signal is invalid/masked by qualifier at window start
				ss << "| - ";
				continue;
			}

			std::string_view lookup_sig = sig;
			if (lookup_sig.starts_with("x:") || lookup_sig.starts_with("b:")) {
				lookup_sig.remove_prefix(2);
			}
			auto name_it = first_sample.signal_full_names.find(sig);
			if (name_it != first_sample.signal_full_names.end() && !name_it->second.empty()) {
				lookup_sig = name_it->second;
			}

			const auto *sig_obj = parser.find_signal(lookup_sig);
			if (!sig_obj) {
				ss << "| - ";
				continue;
			}

			if (has_clock) {
				auto since_tick = parser.get_signal_since_tick(
					*sig_obj, first_sample.timestamp, clock_name, args_.edge, edge_times
				);
				if (!since_tick.has_value()) {
					ss << "| init ";
				} else if (*since_tick == first_sample.tick) {
					ss << "| new ";
				} else {
					ss << std::format("| {} ", *since_tick);
				}
			} else {
				uint64_t since_time = parser.get_signal_since_time(*sig_obj, first_sample.timestamp);
				if (since_time <= parser.get_start_time()) {
					ss << "| init ";
				} else if (since_time == first_sample.timestamp) {
					ss << "| new ";
				} else {
					ss << std::format("| {} ", since_time);
				}
			}
		}
		ss << "|\n";
	}

	// Rows
	auto &label_mgr = verilog::vcd_label_manager::get_instance();

	for (const auto &sample : samples) {
		if (sample.collapsed_count > 0) {
			ss << "| ... | ... ";
			if (args_.edge == "both") {
				ss << "| ... ";
			}
			for (size_t i = 0; i < args_.signals.size(); ++i) {
				if (i == 0) {
					ss << std::format("| *(ticks {}–{}: {} identical cycles collapsed)* ",
					                  sample.tick, sample.end_tick, sample.collapsed_count);
				} else {
					ss << "| ... ";
				}
			}
			ss << "|\n";
			continue;
		}

		ss << std::format("| {} | {} ", sample.tick, sample.timestamp);
		if (args_.edge == "both") {
			ss << std::format("| {} ", sample.edge == "posedge" ? "^" : "v");
		}
		for (const auto &sig : args_.signals) {
			auto val_it = sample.signal_values.find(sig);
			std::string val = (val_it != sample.signal_values.end()) ? val_it->second : "-";

			// Check if a label exists for this signal and value
			std::string_view lookup_sig = sig;
			if (lookup_sig.starts_with("x:") || lookup_sig.starts_with("b:")) {
				lookup_sig.remove_prefix(2);
			}
			auto name_it = sample.signal_full_names.find(sig);
			if (name_it != sample.signal_full_names.end() && !name_it->second.empty()) {
				lookup_sig = name_it->second;
			}

			auto raw_it = sample.signal_raw_values.find(sig);
			std::string raw_bin = (raw_it != sample.signal_raw_values.end()) ? raw_it->second : "";
			std::string label = label_mgr.find_label(args_.safe_path, lookup_sig, val, raw_bin);
			if (!label.empty()) {
				val = std::format("{} ({})", val, label);
			}

			ss << std::format("| {} ", val);
		}
		ss << "|\n";
	}

	// Append expanded shorthand signal mappings if any
	std::map<std::string, std::string> full_names_map;
	for (const auto &sample : samples) {
		for (const auto &[key, full_name] : sample.signal_full_names) {
			if (!full_name.empty()) {
				full_names_map[key] = full_name;
			}
		}
		if (full_names_map.size() == args_.signals.size()) break;
	}

	std::vector<std::string> expanded_items;
	for (const auto &sig : args_.signals) {
		std::string_view display_name = sig;
		if (display_name.starts_with("x:") || display_name.starts_with("b:")) {
			display_name.remove_prefix(2);
		}

		auto it = full_names_map.find(sig);
		std::string full_name = (it != full_names_map.end()) ? it->second : "";
		if (full_name.empty()) {
			const auto *s = parser.find_signal(display_name);
			if (s) full_name = s->full_name;
		}

		if (!full_name.empty() && display_name != full_name) {
			const auto *sig_obj = parser.find_signal(full_name);
			int width = sig_obj ? sig_obj->bit_width : 1;
			expanded_items.push_back(std::format("- `{}` → `{}` ({}-bit)", display_name, full_name, width));
		}
	}

	if (!expanded_items.empty()) {
		ss << "\n#### Resolved Shorthand Signals (Expanded items only)\n";
		for (const auto &item : expanded_items) {
			ss << item << "\n";
		}
	}

	// Generate GTKWave save file if requested
	if (!args_.safe_gtkw.empty()) {
		std::ofstream ofs(args_.safe_gtkw);
		if (!ofs.is_open()) {
			ss << std::format("\n> [!WARNING] Failed to write GTKWave save file to `{}`.\n", args_.requested_gtkw);
		} else {
			ofs << "[*]\n";
			ofs << "[*] GTKWave Analyzer save file generated by TurboStar\n";
			ofs << "[*]\n";
			ofs << std::format("[dumpfile] \"{}\"\n", args_.safe_path);
			ofs << std::format("[savefile] \"{}\"\n", args_.safe_gtkw);

			uint64_t marker_time = 0;
			if (!samples.empty()) {
				marker_time = samples.front().timestamp;
			}
			ofs << std::format("[timestart] {}\n", marker_time);
			ofs << "[size] 1800 900\n";
			ofs << "[pos] -1 -1\n";
			ofs << std::format("*-14.000000 {} -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1\n", marker_time);

			// Collect scopes to open in the tree
			std::set<std::string> tree_scopes;
			for (const auto &sig : args_.signals) {
				std::string_view name = sig;
				if (name.starts_with("x:") || name.starts_with("b:")) {
					name.remove_prefix(2);
				}
				auto it = full_names_map.find(sig);
				std::string full_name = (it != full_names_map.end()) ? it->second : "";
				if (full_name.empty()) {
					const auto *s = parser.find_signal(name);
					if (s) full_name = s->full_name;
				}
				if (!full_name.empty()) {
					size_t pos = 0;
					while ((pos = full_name.find('.', pos)) != std::string::npos) {
						tree_scopes.insert(full_name.substr(0, pos + 1));
						++pos;
					}
				}
			}
			for (const auto &scope : tree_scopes) {
				ofs << std::format("[treeopen] {}\n", scope);
			}

			// Output signals with format flags: @22 = hex, @28 = binary/default
			for (const auto &sig : args_.signals) {
				bool is_hex = sig.starts_with("x:");
				bool is_bin = sig.starts_with("b:");
				std::string_view clean_name = sig;
				if (is_hex || is_bin) {
					clean_name.remove_prefix(2);
				}

				auto it = full_names_map.find(sig);
				std::string full_name = (it != full_names_map.end()) ? it->second : "";
				if (full_name.empty()) {
					const auto *s = parser.find_signal(clean_name);
					if (s) full_name = s->full_name;
				}
				if (full_name.empty()) {
					full_name = std::string(clean_name);
				}

				const auto *sig_obj = parser.find_signal(full_name);
				int width = sig_obj ? sig_obj->bit_width : 1;
				std::string slice = (sig_obj && !sig_obj->slice.empty()) ? std::format("[{}]", sig_obj->slice) : (width > 1 ? std::format("[{}:0]", width - 1) : "");

				if (is_hex || (!is_bin && width > 1)) {
					ofs << "@22\n"; // Hexadecimal display in GTKWave
				} else {
					ofs << "@28\n"; // Standard / binary display in GTKWave
				}
				ofs << std::format("{}{}\n", full_name, slice);
			}
			ofs << "[pattern_trace] 1\n";
			ofs << "[pattern_trace] 0\n";
			ofs.close();

			ss << std::format("\n> [!NOTE] Generated GTKWave save file at `{}` (launch via `gtkwave \"{}\" \"{}\"`).\n",
			                  args_.requested_gtkw, args_.safe_path, args_.requested_gtkw);
		}
	}

	set_success(ctx);
	return ss.str();
}

} // namespace tools
