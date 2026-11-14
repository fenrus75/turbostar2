#include "vcd_wave_tool.h"
#include "vcd_parser.h"
#include "vcd_parser_cache.h"
#include "vcd_label_manager.h"
#include "plugins/verilog/fpga_project_settings.h"
#include "plugins/verilog/verilog_module_scanner.h"
#include <format>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <cctype>
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
	const auto *clk_sig_obj = parser.find_signal(clock_name);
	std::string clock_short_name = clk_sig_obj ? clk_sig_obj->short_name : clock_name;

	std::string preferred_scope;
	std::optional<verilog::resolved_vcd_instance> resolved_inst;
	std::vector<std::string> input_port_names;
	std::vector<std::string> output_port_names;

	if (!args_.instance.empty()) {
		resolved_inst = verilog::verilog_module_scanner::resolve_vcd_instance_scope(
			parser, args_.instance, ctx.fs_security.get_working_directory()
		);
		if (!resolved_inst.has_value()) {
			std::vector<std::string> avail_scopes;
			std::set<std::string> seen;
			for (const auto &s : parser.get_signals()) {
				if (!s.scope.empty() && seen.insert(s.scope).second) {
					avail_scopes.push_back(s.scope);
				}
			}
			std::string scopes_list;
			for (size_t i = 0; i < avail_scopes.size(); ++i) {
				if (i > 0) scopes_list += ", ";
				scopes_list += std::format("`{}`", avail_scopes[i]);
			}
			std::string err_msg = std::format(
				"Error: Instance or module '{}' not found in VCD '{}'. Available scopes: {}",
				args_.instance, args_.requested_path, scopes_list.empty() ? "(none)" : scopes_list
			);
			set_failure(ctx, err_msg);
			return err_msg;
		}

		preferred_scope = resolved_inst->scope;
		std::vector<std::string> auto_signals;
		std::set<std::string> seen_sigs;

		auto try_add_port = [&](const std::string &port_name, const std::string &dir) {
			if (port_name == clock_name || port_name == clock_short_name) {
				return;
			}
			const auto *found = parser.find_signal(port_name, preferred_scope);
			if (!found || found->scope != preferred_scope) {
				return;
			}
			if (seen_sigs.insert(port_name).second) {
				auto_signals.push_back(port_name);
				if (dir == "output") {
					output_port_names.push_back(port_name);
				} else {
					input_port_names.push_back(port_name);
				}
			}
		};

		// Add inputs/inouts first, then outputs
		for (const auto &p : resolved_inst->ports) {
			if (p.direction != "output") {
				try_add_port(p.name, p.direction);
			}
		}
		for (const auto &p : resolved_inst->ports) {
			if (p.direction == "output") {
				try_add_port(p.name, p.direction);
			}
		}

		// Fallback if no RTL source ports were discovered (e.g. standalone VCD without RTL files):
		// include all non-clock signals in the resolved scope.
		if (auto_signals.empty()) {
			for (const auto &sig : parser.get_signals()) {
				if (sig.scope == preferred_scope &&
				    sig.short_name != clock_short_name &&
				    sig.full_name != clock_name &&
				    seen_sigs.insert(sig.short_name).second) {
					auto_signals.push_back(sig.short_name);
				}
			}
		}

		// Append any explicit extra signals passed alongside 'instance'
		for (const auto &extra : args_.signals) {
			std::string_view clean_extra = extra;
			if (clean_extra.starts_with("x:") || clean_extra.starts_with("b:") || clean_extra.starts_with("d:")) {
				clean_extra.remove_prefix(2);
			}
			if (seen_sigs.insert(std::string(clean_extra)).second) {
				auto_signals.push_back(extra);
			}
		}

		args_.signals = std::move(auto_signals);
		if (args_.signals.empty()) {
			std::string err_msg = std::format(
				"Error: No non-clock signals found for instance '{}' (scope '{}') in VCD '{}'.",
				args_.instance, preferred_scope, args_.requested_path
			);
			set_failure(ctx, err_msg);
			return err_msg;
		}
	}

	std::string sample_err;
	char default_radix = 'x';
	if (args_.radix == "bin") {
		default_radix = 'b';
	} else if (args_.radix == "dec") {
		default_radix = 'd';
	}
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
		args_.collapse_idle,
		default_radix,
		preferred_scope
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
	ss << verilog::vcd_parser_cache::check_stale_rtl_warning(args_.safe_path, args_.requested_path);
	ss << std::format("### VCD Waveform: `{}`\n", args_.requested_path);
	if (resolved_inst.has_value()) {
		auto join_backtick = [](const std::vector<std::string> &items) -> std::string {
			if (items.empty()) return "(none)";
			std::string out;
			for (size_t i = 0; i < items.size(); ++i) {
				if (i > 0) out += ", ";
				out += std::format("`{}`", items[i]);
			}
			return out;
		};
		if (!resolved_inst->module_name.empty() && resolved_inst->module_name != resolved_inst->scope) {
			ss << std::format("Instance: `{}` (module `{}`)", resolved_inst->scope, resolved_inst->module_name);
		} else {
			ss << std::format("Instance: `{}`", resolved_inst->scope);
		}
		if (!input_port_names.empty() || !output_port_names.empty()) {
			ss << std::format(" | Inputs: {} | Outputs: {}",
			                  join_backtick(input_port_names), join_backtick(output_port_names));
		}
		ss << "\n";
		if (!resolved_inst->other_scopes.empty()) {
			ss << std::format("> [!NOTE] Multiple instances of `{}` exist in this VCD; showing first instance `{}`. Other instances: {}.\n",
			                  resolved_inst->module_name.empty() ? args_.instance : resolved_inst->module_name,
			                  resolved_inst->scope,
			                  join_backtick(resolved_inst->other_scopes));
		}
	}
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
			if (lookup_sig.starts_with("x:") || lookup_sig.starts_with("b:") || lookup_sig.starts_with("d:")) {
				lookup_sig.remove_prefix(2);
			}
			auto name_it = first_sample.signal_full_names.find(sig);
			if (name_it != first_sample.signal_full_names.end() && !name_it->second.empty()) {
				lookup_sig = name_it->second;
			}

			const auto *sig_obj = parser.find_signal(lookup_sig, preferred_scope);
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
					ss << "| now ";
				} else {
					ss << std::format("| {} ", *since_tick);
				}
			} else {
				uint64_t since_time = parser.get_signal_since_time(*sig_obj, first_sample.timestamp);
				if (since_time <= parser.get_start_time()) {
					ss << "| init ";
				} else if (since_time == first_sample.timestamp) {
					ss << "| now ";
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
			if (lookup_sig.starts_with("x:") || lookup_sig.starts_with("b:") || lookup_sig.starts_with("d:")) {
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
		if (display_name.starts_with("x:") || display_name.starts_with("b:") || display_name.starts_with("d:")) {
			display_name.remove_prefix(2);
		}

		auto it = full_names_map.find(sig);
		std::string full_name = (it != full_names_map.end()) ? it->second : "";
		if (full_name.empty()) {
			const auto *s = parser.find_signal(display_name, preferred_scope);
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
				if (name.starts_with("x:") || name.starts_with("b:") || name.starts_with("d:")) {
					name.remove_prefix(2);
				}
				auto it = full_names_map.find(sig);
				std::string full_name = (it != full_names_map.end()) ? it->second : "";
				if (full_name.empty()) {
					const auto *s = parser.find_signal(name, preferred_scope);
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
				bool is_dec = sig.starts_with("d:");
				std::string_view clean_name = sig;
				if (is_hex || is_bin || is_dec) {
					clean_name.remove_prefix(2);
				}

				auto it = full_names_map.find(sig);
				std::string full_name = (it != full_names_map.end()) ? it->second : "";
				if (full_name.empty()) {
					const auto *s = parser.find_signal(clean_name, preferred_scope);
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

	// One-shot contextual hints (at most one per call; _valid qualifier has higher priority than enum labels)
	struct resolved_sig_info {
		std::string raw_arg;
		std::string clean_arg;
		std::string full_name;
		std::string short_name;
		int bit_width = 1;
	};
	std::vector<resolved_sig_info> resolved_sigs;
	resolved_sigs.reserve(args_.signals.size());
	for (const auto &sig : args_.signals) {
		std::string_view clean_sv = sig;
		if (clean_sv.starts_with("x:") || clean_sv.starts_with("b:") || clean_sv.starts_with("d:")) {
			clean_sv.remove_prefix(2);
		}
		std::string clean_arg(clean_sv);
		auto it = full_names_map.find(sig);
		std::string full_name = (it != full_names_map.end()) ? it->second : "";
		const auto *sig_obj = !full_name.empty() ? parser.find_signal(full_name) : parser.find_signal(clean_arg, preferred_scope);
		if (full_name.empty() && sig_obj) {
			full_name = sig_obj->full_name;
		}
		std::string short_name = sig_obj ? sig_obj->short_name : clean_arg;
		int width = sig_obj ? sig_obj->bit_width : 1;
		resolved_sigs.push_back({sig, std::move(clean_arg), std::move(full_name), std::move(short_name), width});
	}

	bool hint_emitted = false;
	for (const auto &v_info : resolved_sigs) {
		if (!v_info.clean_arg.ends_with("_valid") && !v_info.short_name.ends_with("_valid")) {
			continue;
		}
		for (const auto &b_info : resolved_sigs) {
			bool matches_base =
				(b_info.clean_arg + "_valid" == v_info.clean_arg) ||
				(!b_info.full_name.empty() && b_info.full_name + "_valid" == v_info.full_name) ||
				(b_info.short_name + "_valid" == v_info.short_name);
			if (!matches_base) {
				continue;
			}
			bool has_qual = !label_mgr.find_qualifier(args_.safe_path, b_info.full_name).first.empty() ||
			                !label_mgr.find_qualifier(args_.safe_path, b_info.clean_arg).first.empty();
			if (!has_qual && label_mgr.try_consume_qualifier_hint()) {
				if (verilog::fpga_project_settings::is_cli_mode()) {
					ss << std::format(
						"\n> [!TIP] Both `{}` and `{}` were requested. To mask invalid cycles of `{}` as `-` (and collapse idle rows automatically), run:\n"
						"> `turbostar-verilog vcd-qualifier {} -s {} -q {}`\n",
						b_info.clean_arg, v_info.clean_arg, b_info.clean_arg,
						args_.requested_path, b_info.clean_arg, v_info.clean_arg);
				} else {
					ss << std::format(
						"\n> [!TIP] Both `{}` and `{}` were requested. To mask invalid cycles of `{}` as `-` (and collapse idle rows automatically), use `vcd_signal_qualifier(path=\"{}\", signal=\"{}\", qualifier=\"{}\")`.\n",
						b_info.clean_arg, v_info.clean_arg, b_info.clean_arg,
						args_.requested_path, b_info.clean_arg, v_info.clean_arg);
				}
				hint_emitted = true;
			}
			break;
		}
		if (hint_emitted) {
			break;
		}
	}

	if (!hint_emitted) {
		for (const auto &s_info : resolved_sigs) {
			if (s_info.bit_width <= 1) {
				continue;
			}
			std::string lower_short;
			lower_short.reserve(s_info.short_name.size());
			for (unsigned char c : s_info.short_name) {
				lower_short.push_back(static_cast<char>(std::tolower(c)));
			}
			bool is_enum_candidate =
				lower_short.find("state") != std::string::npos ||
				lower_short.find("opcode") != std::string::npos ||
				lower_short.find("instr") != std::string::npos ||
				lower_short.find("fsm") != std::string::npos;
			if (!is_enum_candidate) {
				continue;
			}
			bool has_lbl = label_mgr.has_labels_for_signal(args_.safe_path, s_info.full_name) ||
			               label_mgr.has_labels_for_signal(args_.safe_path, s_info.clean_arg);
			if (!has_lbl && label_mgr.try_consume_label_hint()) {
				std::vector<std::string> sample_hex;
				for (const auto &sample : samples) {
					if (sample.collapsed_count > 0) {
						continue;
					}
					auto vit = sample.signal_values.find(s_info.raw_arg);
					if (vit != sample.signal_values.end() && vit->second.starts_with("0x") &&
					    vit->second.find('x', 2) == std::string::npos &&
					    vit->second.find('z', 2) == std::string::npos) {
						if (std::find(sample_hex.begin(), sample_hex.end(), vit->second) == sample_hex.end()) {
							sample_hex.push_back(vit->second);
							if (sample_hex.size() >= 2) {
								break;
							}
						}
					}
				}
				if (verilog::fpga_project_settings::is_cli_mode()) {
					std::string cli_labels_example = sample_hex.empty()
						? "0x00=IDLE,0x01=ACTIVE"
						: (sample_hex.size() == 1
							? std::format("{}=<NAME_1>", sample_hex[0])
							: std::format("{}=<NAME_1>,{}=<NAME_2>", sample_hex[0], sample_hex[1]));
					ss << std::format(
						"\n> [!TIP] Signal `{}` has no symbolic labels yet. To display enum names alongside hex values (and search by name in `turbostar-verilog vcd-search`), run:\n"
						"> `turbostar-verilog vcd-labels {} -s {} -l \"{}\"`\n",
						s_info.clean_arg, args_.requested_path, s_info.clean_arg, cli_labels_example);
				} else {
					std::string labels_example = sample_hex.empty()
						? R"({"0x00": "IDLE", "0x01": "ACTIVE"})"
						: (sample_hex.size() == 1
							? std::format(R"({{"{}": "<NAME_1>"}})", sample_hex[0])
							: std::format(R"({{"{}": "<NAME_1>", "{}": "<NAME_2>"}})", sample_hex[0], sample_hex[1]));
					ss << std::format(
						"\n> [!TIP] Signal `{}` has no symbolic labels yet. To display enum names alongside hex values (and search by name in `vcd_search`), use `vcd_signal_labels(path=\"{}\", signal=\"{}\", labels={})`.\n",
						s_info.clean_arg, args_.requested_path, s_info.clean_arg, labels_example);
				}
				hint_emitted = true;
				break;
			}
		}
	}

	set_success(ctx);
	return ss.str();
}

} // namespace tools
