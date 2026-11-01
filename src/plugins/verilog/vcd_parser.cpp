#include "vcd_parser.h"
#include <algorithm>
#include <fstream>
#include <format>
#include <sstream>
#include <cctype>
#include <ctre.hpp>

namespace verilog
{

namespace
{

std::string binary_to_hex(std::string_view bin)
{
	if (bin.empty()) return "0x0";
	// Strip leading 'b' or 'B' if present
	if (bin.front() == 'b' || bin.front() == 'B') {
		bin.remove_prefix(1);
	}
	// Pad binary string to multiple of 4 bits
	size_t rem = bin.size() % 4;
	std::string padded;
	if (rem != 0) {
		padded.append(4 - rem, '0');
	}
	padded.append(bin);

	std::string hex = "0x";
	for (size_t i = 0; i < padded.size(); i += 4) {
		std::string_view nibble = std::string_view(padded).substr(i, 4);
		if (nibble.find('x') != std::string_view::npos || nibble.find('X') != std::string_view::npos) {
			hex.push_back('x');
		} else if (nibble.find('z') != std::string_view::npos || nibble.find('Z') != std::string_view::npos) {
			hex.push_back('z');
		} else {
			int val = 0;
			for (char c : nibble) {
				val = (val << 1) | (c == '1' ? 1 : 0);
			}
			if (val < 10) hex.push_back(static_cast<char>('0' + val));
			else hex.push_back(static_cast<char>('A' + (val - 10)));
		}
	}
	return hex;
}

// Simple wildcard match for glob (* and ?)
bool wildcard_match(std::string_view pattern, std::string_view text)
{
	size_t p = 0, t = 0;
	size_t star_p = std::string_view::npos, match_t = 0;
	while (t < text.size()) {
		if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) {
			++p;
			++t;
		} else if (p < pattern.size() && pattern[p] == '*') {
			star_p = p++;
			match_t = t;
		} else if (star_p != std::string_view::npos) {
			p = star_p + 1;
			t = ++match_t;
		} else {
			return false;
		}
	}
	while (p < pattern.size() && pattern[p] == '*') {
		++p;
	}
	return p == pattern.size();
}

} // namespace

bool vcd_parser::parse_header(std::string_view untrusted_path, std::string &out_error)
{
	std::ifstream file(std::string(untrusted_path), std::ios::in);
	if (!file.is_open()) {
		out_error = std::format("Failed to open VCD file: {}", untrusted_path);
		return false;
	}

	std::string content;
	std::string line;
	while (std::getline(file, line)) {
		content.append(line);
		content.push_back('\n');
		if (line.find("$enddefinitions") != std::string::npos) {
			break;
		}
	}

	parse_header_tokens(content, out_error);
	if (!out_error.empty()) {
		return false;
	}

	header_parsed_ = true;
	file_path_ = std::string(untrusted_path);
	return true;
}

bool vcd_parser::parse_all(std::string_view untrusted_path, std::string &out_error)
{
	std::ifstream file(std::string(untrusted_path), std::ios::in);
	if (!file.is_open()) {
		out_error = std::format("Failed to open VCD file: {}", untrusted_path);
		return false;
	}

	std::stringstream buffer;
	buffer << file.rdbuf();
	std::string content = buffer.str();

	parse_header_tokens(content, out_error);
	if (!out_error.empty()) {
		return false;
	}

	header_parsed_ = true;
	file_path_ = std::string(untrusted_path);

	parse_body_tokens(content);
	data_parsed_ = true;
	return true;
}

void vcd_parser::parse_header_tokens(const std::string &content, std::string &/*out_error*/)
{
	signals_.clear();
	id_to_signals_.clear();
	name_to_signal_idx_.clear();

	std::vector<std::string> scope_stack;
	std::istringstream stream(content);
	std::string token;

	while (stream >> token) {
		if (token == "$enddefinitions") {
			while (stream >> token && token != "$end") {}
			break;
		} else if (token == "$timescale") {
			std::string ts;
			while (stream >> token && token != "$end") {
				if (!ts.empty()) ts.push_back(' ');
				ts.append(token);
			}
			if (!ts.empty()) timescale_ = ts;
		} else if (token == "$date") {
			std::string d;
			while (stream >> token && token != "$end") {
				if (!d.empty()) d.push_back(' ');
				d.append(token);
			}
			date_ = d;
		} else if (token == "$version") {
			std::string v;
			while (stream >> token && token != "$end") {
				if (!v.empty()) v.push_back(' ');
				v.append(token);
			}
			version_ = v;
		} else if (token == "$scope") {
			std::string type, name;
			stream >> type >> name;
			while (stream >> token && token != "$end") {}
			scope_stack.push_back(name);
		} else if (token == "$upscope") {
			while (stream >> token && token != "$end") {}
			if (!scope_stack.empty()) {
				scope_stack.pop_back();
			}
		} else if (token == "$var") {
			std::string var_type, size_str, id_code, ref;
			stream >> var_type >> size_str >> id_code >> ref;
			std::string slice;
			while (stream >> token && token != "$end") {
				if (token.front() == '[') {
					slice = token;
				}
			}

			int bit_width = 1;
			try {
				bit_width = std::stoi(size_str);
			} catch (...) {}

			std::string full_scope;
			for (const auto &sc : scope_stack) {
				if (!full_scope.empty()) full_scope.push_back('.');
				full_scope.append(sc);
			}

			std::string full_name = full_scope.empty() ? ref : std::format("{}.{}", full_scope, ref);

			vcd_signal sig;
			sig.full_name = full_name;
			sig.short_name = ref;
			sig.scope = full_scope;
			sig.id_code = id_code;
			sig.var_type = var_type;
			sig.bit_width = bit_width;
			sig.slice = slice;

			size_t idx = signals_.size();
			signals_.push_back(sig);
			id_to_signals_[id_code].push_back(idx);
			name_to_signal_idx_[full_name] = idx;
		} else {
			// Skip unknown token until $end if needed
			if (token.front() == '$') {
				while (stream >> token && token != "$end") {}
			}
		}
	}
}

void vcd_parser::parse_body_tokens(const std::string &content)
{
	id_transitions_.clear();
	timestamps_.clear();

	// Find the end of definitions
	size_t pos = content.find("$enddefinitions");
	if (pos != std::string::npos) {
		pos = content.find("$end", pos + 15);
		if (pos != std::string::npos) {
			pos += 4;
		}
	} else {
		pos = 0;
	}

	uint64_t current_time = 0;
	bool has_time = false;

	// Parse simulation tokens
	std::string remaining_str(content.data() + pos, content.size() - pos);
	std::istringstream stream(remaining_str);
	std::string token;

	while (stream >> token) {
		if (token.empty()) continue;

		if (token.front() == '#') {
			try {
				current_time = std::stoull(token.substr(1));
				if (!has_time) {
					start_time_ = current_time;
					has_time = true;
				}
				end_time_ = current_time;
				if (timestamps_.empty() || timestamps_.back() != current_time) {
					timestamps_.push_back(current_time);
				}
			} catch (...) {}
		} else if (token.front() == 'b' || token.front() == 'B' || token.front() == 'r' || token.front() == 'R') {
			// Vector or real format: b0011 <id>
			std::string val = token.substr(1);
			std::string id;
			if (stream >> id) {
				id_transitions_[id][current_time] = val;
			}
		} else if (token.front() == '0' || token.front() == '1' || token.front() == 'x' || token.front() == 'X' || token.front() == 'z' || token.front() == 'Z') {
			if (token.size() > 1) {
				// Scalar format: 0<id> or 1<id>
				char val = token[0];
				std::string id = token.substr(1);
				id_transitions_[id][current_time] = std::string(1, val);
			}
		} else if (token.front() == '$') {
			// Skip dump commands like $dumpvars, $dumpall, $end
			std::string skip;
			while (stream >> skip && skip != "$end") {}
		}
	}
}

const vcd_signal *vcd_parser::find_signal(std::string_view name_or_path) const
{
	auto it = name_to_signal_idx_.find(std::string(name_or_path));
	if (it != name_to_signal_idx_.end()) {
		return &signals_[it->second];
	}

	// Try searching by short name if unambiguous
	const vcd_signal *matched = nullptr;
	for (const auto &sig : signals_) {
		if (sig.short_name == name_or_path) {
			if (matched != nullptr) {
				return nullptr; // Ambiguous match
			}
			matched = &sig;
		}
	}
	return matched;
}

std::vector<const vcd_signal *> vcd_parser::filter_signals(std::string_view scope_filter, std::string_view glob_pattern) const
{
	std::vector<const vcd_signal *> result;
	for (const auto &sig : signals_) {
		if (!scope_filter.empty()) {
			if (sig.scope != scope_filter && !sig.scope.starts_with(std::string(scope_filter) + ".")) {
				continue;
			}
		}
		if (!glob_pattern.empty()) {
			if (!wildcard_match(glob_pattern, sig.full_name) && !wildcard_match(glob_pattern, sig.short_name)) {
				continue;
			}
		}
		result.push_back(&sig);
	}
	return result;
}

std::optional<vcd_clock_info> vcd_parser::analyze_clock(std::string_view clock_signal_name) const
{
	const vcd_signal *sig = find_signal(clock_signal_name);
	if (!sig) return std::nullopt;

	auto it = id_transitions_.find(sig->id_code);
	if (it == id_transitions_.end()) return std::nullopt;

	const auto &transitions = it->second;
	vcd_clock_info info;
	info.signal_name = sig->full_name;

	std::string last_val;
	std::vector<uint64_t> posedges;
	std::vector<uint64_t> negedges;

	for (const auto &[time, val] : transitions) {
		if (val == "1" && last_val == "0") {
			posedges.push_back(time);
		} else if (val == "0" && last_val == "1") {
			negedges.push_back(time);
		}
		last_val = val;
	}

	info.posedge_count = posedges.size();
	info.negedge_count = negedges.size();

	if (posedges.size() >= 2) {
		info.first_edge_time = posedges[0];
		// Calculate average period over available posedges
		uint64_t total_delta = posedges.back() - posedges.front();
		info.period = total_delta / (posedges.size() - 1);
	} else if (!posedges.empty()) {
		info.first_edge_time = posedges[0];
		info.period = 0;
	}

	return info;
}

std::optional<vcd_clock_info> vcd_parser::detect_clock() const
{
	// Candidate signals: 1-bit wire/reg with name containing "clk" or "clock", or highest toggle rate
	std::string best_clock;
	size_t max_toggles = 0;

	for (const auto &sig : signals_) {
		if (sig.bit_width != 1) continue;
		std::string lower_name = sig.short_name;
		std::ranges::transform(lower_name, lower_name.begin(), [](unsigned char c) { return std::tolower(c); });

		auto it = id_transitions_.find(sig.id_code);
		if (it == id_transitions_.end()) continue;

		size_t toggles = it->second.size();
		if (lower_name.find("clk") != std::string::npos || lower_name.find("clock") != std::string::npos) {
			if (toggles > max_toggles) {
				max_toggles = toggles;
				best_clock = sig.full_name;
			}
		}
	}

	if (!best_clock.empty()) {
		return analyze_clock(best_clock);
	}

	// Fallback to 1-bit signal with highest transition count
	for (const auto &sig : signals_) {
		if (sig.bit_width != 1) continue;
		auto it = id_transitions_.find(sig.id_code);
		if (it != id_transitions_.end() && it->second.size() > max_toggles) {
			max_toggles = it->second.size();
			best_clock = sig.full_name;
		}
	}

	if (!best_clock.empty() && max_toggles >= 2) {
		return analyze_clock(best_clock);
	}

	return std::nullopt;
}

std::vector<vcd_sample_point> vcd_parser::sample_wave(
	std::string_view clock_signal,
	std::string_view edge_mode,
	const std::vector<std::string> &requested_signals,
	std::optional<size_t> start_tick,
	std::optional<size_t> end_tick,
	std::optional<uint64_t> start_time,
	std::optional<uint64_t> end_time,
	size_t max_ticks,
	std::string &out_error
) const
{
	const vcd_signal *clk_sig = find_signal(clock_signal);
	if (!clk_sig) {
		out_error = std::format("Clock signal not found: {}", clock_signal);
		return {};
	}

	auto clk_it = id_transitions_.find(clk_sig->id_code);
	if (clk_it == id_transitions_.end()) {
		out_error = std::format("No transitions recorded for clock: {}", clock_signal);
		return {};
	}

	// Prepare signal mappings and formats
	struct requested_desc {
		std::string key;
		char format_override{' '}; // 'x' for hex, 'b' for binary
		const vcd_signal *signal{nullptr};
	};

	std::vector<requested_desc> reqs;
	for (const auto &req_str : requested_signals) {
		requested_desc desc;
		desc.key = req_str;
		std::string_view lookup_name = req_str;
		if (lookup_name.starts_with("x:") || lookup_name.starts_with("b:")) {
			desc.format_override = lookup_name[0];
			lookup_name.remove_prefix(2);
		}
		desc.signal = find_signal(lookup_name);
		if (!desc.signal) {
			out_error = std::format("Signal '{}' not found in VCD file", req_str);
			return {};
		}
		reqs.push_back(desc);
	}

	// Collect sample events (timestamps and edges)
	struct edge_event {
		uint64_t timestamp;
		std::string edge;
	};
	std::vector<edge_event> sample_events;

	std::string last_clk_val;
	for (const auto &[time, val] : clk_it->second) {
		bool is_posedge = (val == "1" && last_clk_val == "0");
		bool is_negedge = (val == "0" && last_clk_val == "1");

		if (is_posedge && (edge_mode == "posedge" || edge_mode == "both")) {
			sample_events.push_back({time, "posedge"});
		} else if (is_negedge && (edge_mode == "negedge" || edge_mode == "both")) {
			sample_events.push_back({time, "negedge"});
		}
		last_clk_val = val;
	}

	// Apply filtering (tick range, time range, max_ticks)
	size_t first_tick = start_tick.value_or(0);
	size_t last_tick = end_tick.value_or(sample_events.size() > 0 ? sample_events.size() - 1 : 0);

	std::vector<vcd_sample_point> results;

	// State tracking for all requested signal IDs
	std::map<std::string, std::string> current_values; // id_code -> current raw string
	auto time_it = timestamps_.begin();

	for (size_t i = 0; i < sample_events.size(); ++i) {
		const auto &event = sample_events[i];

		// Advance state of all signals up to event.timestamp
		while (time_it != timestamps_.end() && *time_it <= event.timestamp) {
			uint64_t t = *time_it;
			for (const auto &r : reqs) {
				const auto &trans_map = id_transitions_.find(r.signal->id_code);
				if (trans_map != id_transitions_.end()) {
					auto v_it = trans_map->second.find(t);
					if (v_it != trans_map->second.end()) {
						current_values[r.signal->id_code] = v_it->second;
					}
				}
			}
			++time_it;
		}

		// Filter by tick index
		if (i < first_tick) continue;
		if (i > last_tick) break;

		// Filter by time window if specified
		if (start_time.has_value() && event.timestamp < *start_time) continue;
		if (end_time.has_value() && event.timestamp > *end_time) break;

		if (results.size() >= max_ticks) break;

		vcd_sample_point pt;
		pt.tick = i;
		pt.timestamp = event.timestamp;
		pt.edge = event.edge;

		for (const auto &r : reqs) {
			std::string raw = current_values[r.signal->id_code];
			if (raw.empty()) raw = "x";

			std::string formatted;
			if (r.format_override == 'b') {
				formatted = raw;
			} else if (r.format_override == 'x') {
				formatted = binary_to_hex(raw);
			} else {
				// Default format: Hex for multi-bit, raw 0/1/x/z for 1-bit
				if (r.signal->bit_width > 1) {
					formatted = binary_to_hex(raw);
				} else {
					formatted = raw;
				}
			}
			pt.signal_values[r.key] = formatted;
		}

		results.push_back(std::move(pt));
	}

	return results;
}

} // namespace verilog
