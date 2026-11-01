#include "vcd_label_manager.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <format>

namespace verilog
{

vcd_label_manager &vcd_label_manager::get_instance()
{
	static vcd_label_manager instance;
	return instance;
}

namespace
{

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

// Checks if a binary pattern with '?' wildcards matches a raw binary value
// e.g. pattern "00????10" matches "00110110"
bool vcd_label_manager::binary_pattern_matches(std::string_view pattern, std::string_view bin_val)
{
	std::string p(pattern);
	if (p.starts_with('b') || p.starts_with('B')) {
		p.erase(0, 1);
	}
	std::string v(bin_val);
	if (v.starts_with('b') || v.starts_with('B')) {
		v.erase(0, 1);
	}

	// Pad the shorter string on the left with '0'
	if (p.size() < v.size()) {
		p.insert(0, v.size() - p.size(), '0');
	} else if (v.size() < p.size()) {
		v.insert(0, p.size() - v.size(), '0');
	}

	for (size_t i = 0; i < p.size(); ++i) {
		if (p[i] == '?' || p[i] == 'x' || p[i] == 'X') {
			continue;
		}
		if (p[i] != v[i]) {
			return false;
		}
	}
	return true;
}

bool vcd_label_manager::value_matches(
	std::string_view target_val,
	std::string_view hex_val,
	std::string_view raw_bin_val,
	std::string_view label
)
{
	if (target_val.empty()) {
		return false;
	}

	// 1. Label match (case-insensitive)
	if (!label.empty()) {
		if (target_val.size() == label.size()) {
			bool eq = true;
			for (size_t i = 0; i < target_val.size(); ++i) {
				if (std::tolower(static_cast<unsigned char>(target_val[i])) !=
				    std::tolower(static_cast<unsigned char>(label[i]))) {
					eq = false;
					break;
				}
			}
			if (eq) return true;
		}
	}

	// 2. Exact match against hex_val or raw_bin_val
	if (target_val == hex_val || (!raw_bin_val.empty() && target_val == raw_bin_val)) {
		return true;
	}

	// 3. Hex normalized match
	std::string norm_target = normalize_hex_key(target_val);
	std::string norm_hex = normalize_hex_key(hex_val);
	if (!norm_target.empty() && !norm_hex.empty() && norm_target == norm_hex) {
		return true;
	}

	// 4. Binary pattern with wildcards
	if (target_val.find('?') != std::string::npos || target_val.starts_with('b') || target_val.starts_with('B')) {
		if (!raw_bin_val.empty() && binary_pattern_matches(target_val, raw_bin_val)) {
			return true;
		}
		// Also try converting hex_val to binary if raw_bin_val is empty
		if (hex_val.starts_with("0x")) {
			try {
				uint64_t n = std::stoull(std::string(hex_val), nullptr, 16);
				std::string gen_bin;
				for (int b = 63; b >= 0; --b) {
					gen_bin.push_back((n & (1ULL << b)) ? '1' : '0');
				}
				size_t f1 = gen_bin.find('1');
				if (f1 != std::string::npos) gen_bin = gen_bin.substr(f1);
				else gen_bin = "0";
				if (binary_pattern_matches(target_val, gen_bin)) {
					return true;
				}
			} catch (...) {}
		}
	}

	return false;
}

bool vcd_label_manager::signal_pattern_matches(std::string_view pattern, std::string_view signal_name)
{
	if (pattern.empty() || signal_name.empty()) {
		return false;
	}
	if (pattern == signal_name) {
		return true;
	}
	if (pattern.find('*') != std::string::npos || pattern.find('?') != std::string::npos) {
		if (wildcard_match(pattern, signal_name)) {
			return true;
		}
		// Also check if pattern matches with a leading wildcard (e.g. pattern "uut.*.A_val" matching "tb_main.uut.cpu8008.A_val")
		std::string wild_prefix = "*" + std::string(pattern);
		if (wildcard_match(wild_prefix, signal_name)) {
			return true;
		}
		// Also check leaf name against wildcard pattern
		size_t last_dot = signal_name.rfind('.');
		if (last_dot != std::string_view::npos) {
			std::string_view leaf = signal_name.substr(last_dot + 1);
			if (wildcard_match(pattern, leaf)) {
				return true;
			}
		}
		return false;
	}
	if (signal_name.ends_with("." + std::string(pattern)) || pattern.ends_with("." + std::string(signal_name))) {
		return true;
	}
	// Check leaf short name equality
	size_t last_dot = signal_name.rfind('.');
	if (last_dot != std::string_view::npos) {
		std::string_view leaf = signal_name.substr(last_dot + 1);
		if (leaf == pattern) {
			return true;
		}
	}
	return false;
}

bool vcd_label_manager::is_qualifier_valid(std::string_view qualifier_raw_val, bool active_low)
{
	// Single-bit qualifier: active_low expects "0", active_high expects "1"
	// "x", "z", or empty are always invalid
	if (active_low) {
		return qualifier_raw_val == "0";
	} else {
		return qualifier_raw_val == "1";
	}
}

std::string vcd_label_manager::normalize_hex_key(std::string_view val)
{
	std::string s(val);
	// Trim whitespace
	size_t start = s.find_first_not_of(" \t\r\n");
	if (start == std::string::npos) return "";
	size_t end = s.find_last_not_of(" \t\r\n");
	s = s.substr(start, end - start + 1);

	// Lowercase
	std::ranges::transform(s, s.begin(), [](unsigned char c) { return std::tolower(c); });

	// If contains '?', keep as binary wildcard pattern
	if (s.find('?') != std::string::npos) {
		if (s.starts_with('b')) s.erase(0, 1);
		return "b" + s;
	}

	// If already starts with 0x, return as is
	if (s.starts_with("0x")) {
		return s;
	}

	// Try converting binary string (e.g. "00110110" or "b00110110")
	if (s.starts_with('b')) {
		s.erase(0, 1);
		return "b" + s;
	}

	bool is_bin = !s.empty() && std::ranges::all_of(s, [](char c) { return c == '0' || c == '1'; });
	if (is_bin) {
		try {
			uint64_t n = std::stoull(s, nullptr, 2);
			return std::format("0x{:02x}", n);
		} catch (...) {}
	}

	// Try parsing decimal / hex without prefix
	try {
		uint64_t n = std::stoull(s, nullptr, 16);
		return std::format("0x{:02x}", n);
	} catch (...) {}

	return s;
}

void vcd_label_manager::load_sidecar_locked(std::string_view safe_path)
{
	std::string p(safe_path);
	if (file_loaded_[p]) return;
	file_loaded_[p] = true;

	std::string sidecar_path = p + ".labels.json";
	if (!std::filesystem::exists(sidecar_path)) {
		return;
	}

	std::ifstream file(sidecar_path);
	if (!file.is_open()) return;

	try {
		nlohmann::json j;
		file >> j;
		if (j.is_object()) {
			if (j.contains("labels") || j.contains("qualifiers")) {
				if (j.contains("labels") && j["labels"].is_object()) {
					for (auto &[sig, val_map] : j["labels"].items()) {
						if (val_map.is_object()) {
							for (auto &[val_str, label_str] : val_map.items()) {
								if (label_str.is_string()) {
									std::string norm = normalize_hex_key(val_str);
									path_labels_[p][sig][norm] = label_str.get<std::string>();
								}
							}
						}
					}
				}
				if (j.contains("qualifiers") && j["qualifiers"].is_object()) {
					for (auto &[sig, q_str] : j["qualifiers"].items()) {
						if (q_str.is_string()) {
							path_qualifiers_[p][sig] = q_str.get<std::string>();
						}
					}
				}
			} else {
				// Legacy format where entire root is signal -> val_map
				for (auto &[sig, val_map] : j.items()) {
					if (val_map.is_object()) {
						for (auto &[val_str, label_str] : val_map.items()) {
							if (label_str.is_string()) {
								std::string norm = normalize_hex_key(val_str);
								path_labels_[p][sig][norm] = label_str.get<std::string>();
							}
						}
					}
				}
			}
		}
	} catch (...) {}
}

bool vcd_label_manager::save_sidecar_locked(std::string_view safe_path, std::string &out_error)
{
	std::string p(safe_path);
	std::string sidecar_path = p + ".labels.json";

	const auto &sig_map = path_labels_[p];
	const auto &qual_map = path_qualifiers_[p];
	if (sig_map.empty() && qual_map.empty()) {
		// If both maps are completely empty, remove sidecar file if it exists
		if (std::filesystem::exists(sidecar_path)) {
			std::error_code ec;
			std::filesystem::remove(sidecar_path, ec);
		}
		return true;
	}

	nlohmann::json j = nlohmann::json::object();

	nlohmann::json labels_obj = nlohmann::json::object();
	for (const auto &[sig, val_map] : sig_map) {
		nlohmann::json v_obj = nlohmann::json::object();
		for (const auto &[val_key, label] : val_map) {
			v_obj[val_key] = label;
		}
		labels_obj[sig] = v_obj;
	}
	j["labels"] = labels_obj;

	nlohmann::json quals_obj = nlohmann::json::object();
	for (const auto &[sig, q_expr] : qual_map) {
		quals_obj[sig] = q_expr;
	}
	j["qualifiers"] = quals_obj;

	std::ofstream file(sidecar_path, std::ios::trunc);
	if (!file.is_open()) {
		out_error = std::format("Failed to open sidecar file for writing: {}", sidecar_path);
		return false;
	}

	file << j.dump(2) << "\n";
	return true;
}

std::map<std::string, std::map<std::string, std::string>> vcd_label_manager::get_labels_for_file(std::string_view safe_path)
{
	std::lock_guard<std::mutex> lock(mutex_);
	load_sidecar_locked(safe_path);
	return path_labels_[std::string(safe_path)];
}

std::string vcd_label_manager::find_label(
	std::string_view safe_path,
	std::string_view signal_name,
	std::string_view hex_val,
	std::string_view raw_bin_val
)
{
	std::lock_guard<std::mutex> lock(mutex_);
	std::string p(safe_path);
	load_sidecar_locked(p);

	const auto &sig_map = path_labels_[p];
	std::string norm_hex = normalize_hex_key(hex_val);

	// Helper to search a specific signal's val_map
	auto match_in_val_map = [&](const std::map<std::string, std::string> &val_map) -> std::string {
		// 1. Direct match on normalized hex key
		auto it = val_map.find(norm_hex);
		if (it != val_map.end()) {
			return it->second;
		}

		// 2. Binary wildcard pattern match
		for (const auto &[pattern, label] : val_map) {
			if (pattern.starts_with('b') && pattern.find('?') != std::string::npos) {
				if (!raw_bin_val.empty() && binary_pattern_matches(pattern, raw_bin_val)) {
					return label;
				}
				// Also try against hex_val converted to binary
				if (hex_val.starts_with("0x")) {
					try {
						uint64_t n = std::stoull(std::string(hex_val), nullptr, 16);
						std::string gen_bin;
						for (int b = 63; b >= 0; --b) {
							gen_bin.push_back((n & (1ULL << b)) ? '1' : '0');
						}
						// Strip leading zeros
						size_t f1 = gen_bin.find('1');
						if (f1 != std::string::npos) gen_bin = gen_bin.substr(f1);
						else gen_bin = "0";
						if (binary_pattern_matches(pattern, gen_bin)) {
							return label;
						}
					} catch (...) {}
				}
			}
		}
		return "";
	};

	// 1. Try exact signal name match
	auto it = sig_map.find(std::string(signal_name));
	if (it != sig_map.end()) {
		std::string res = match_in_val_map(it->second);
		if (!res.empty()) return res;
	}

	// 2. Try matching by leaf short name, suffix, or glob wildcard pattern (e.g. "module1.*.opcode" or "*opcode")
	for (const auto &[reg_sig, val_map] : sig_map) {
		if (reg_sig == signal_name) {
			continue; // Already checked exact match
		}
		if (signal_pattern_matches(reg_sig, signal_name)) {
			std::string res = match_in_val_map(val_map);
			if (!res.empty()) return res;
		}
	}

	return "";
}

bool vcd_label_manager::set_labels(
	std::string_view safe_path,
	std::string_view signal_name,
	const std::map<std::string, std::string> &val_to_label,
	bool clear,
	bool &out_file_saved,
	std::string &out_msg
)
{
	std::lock_guard<std::mutex> lock(mutex_);
	std::string p(safe_path);
	load_sidecar_locked(p);

	if (clear) {
		if (signal_name.empty()) {
			path_labels_[p].clear();
			out_msg = "Cleared all signal labels for this VCD file.";
		} else {
			path_labels_[p].erase(std::string(signal_name));
			out_msg = std::format("Cleared labels for signal '{}'.", signal_name);
		}
	} else {
		for (const auto &[val_raw, label] : val_to_label) {
			std::string norm = normalize_hex_key(val_raw);
			path_labels_[p][std::string(signal_name)][norm] = label;
		}
		out_msg = std::format("Registered {} label(s) for signal '{}'.", val_to_label.size(), signal_name);
	}

	// Attempt sidecar save
	std::string save_err;
	out_file_saved = save_sidecar_locked(p, save_err);
	if (out_file_saved) {
		out_msg += " Saved to sidecar file.";
	} else {
		out_msg += std::format(" Retained in-memory only (could not write sidecar: {}).", save_err);
	}

	return true;
}

std::map<std::string, std::string> vcd_label_manager::get_qualifiers_for_file(std::string_view safe_path)
{
	std::lock_guard<std::mutex> lock(mutex_);
	load_sidecar_locked(safe_path);
	return path_qualifiers_[std::string(safe_path)];
}

std::pair<std::string, bool> vcd_label_manager::find_qualifier(std::string_view safe_path, std::string_view signal_name)
{
	std::lock_guard<std::mutex> lock(mutex_);
	std::string p(safe_path);
	load_sidecar_locked(p);

	const auto &qual_map = path_qualifiers_[p];
	if (qual_map.empty() || signal_name.empty()) {
		return {"", false};
	}

	auto parse_expr = [](std::string_view expr) -> std::pair<std::string, bool> {
		bool active_low = false;
		std::string s(expr);
		if (s.starts_with('~') || s.starts_with('!')) {
			active_low = true;
			s.erase(0, 1);
		}
		return {s, active_low};
	};

	// 1. Exact match
	auto it = qual_map.find(std::string(signal_name));
	if (it != qual_map.end()) {
		return parse_expr(it->second);
	}

	// 2. Pattern / wildcard / suffix match
	for (const auto &[reg_sig, q_expr] : qual_map) {
		if (signal_pattern_matches(reg_sig, signal_name)) {
			return parse_expr(q_expr);
		}
	}

	return {"", false};
}

bool vcd_label_manager::set_qualifier(
	std::string_view safe_path,
	std::string_view signal_name,
	std::string_view qualifier_expr,
	bool clear,
	bool &out_file_saved,
	std::string &out_msg
)
{
	std::lock_guard<std::mutex> lock(mutex_);
	std::string p(safe_path);
	load_sidecar_locked(p);

	if (clear) {
		if (signal_name.empty()) {
			path_qualifiers_[p].clear();
			out_msg = "Cleared all signal qualifiers for this VCD file.";
		} else {
			path_qualifiers_[p].erase(std::string(signal_name));
			out_msg = std::format("Cleared qualifier for signal '{}'.", signal_name);
		}
	} else {
		path_qualifiers_[p][std::string(signal_name)] = std::string(qualifier_expr);
		out_msg = std::format("Registered qualifier '{}' for signal '{}'.", qualifier_expr, signal_name);
	}

	std::string save_err;
	out_file_saved = save_sidecar_locked(p, save_err);
	if (out_file_saved) {
		out_msg += " Saved to sidecar file.";
	} else {
		out_msg += std::format(" Retained in-memory only (could not write sidecar: {}).", save_err);
	}

	return true;
}

bool vcd_label_manager::set_qualifiers_batch(
	std::string_view safe_path,
	const std::map<std::string, std::string> &bindings,
	bool &out_file_saved,
	std::string &out_msg
)
{
	std::lock_guard<std::mutex> lock(mutex_);
	std::string p(safe_path);
	load_sidecar_locked(p);

	for (const auto &[sig, q] : bindings) {
		path_qualifiers_[p][sig] = q;
	}
	out_msg = std::format("Registered {} signal qualifier binding(s).", bindings.size());

	std::string save_err;
	out_file_saved = save_sidecar_locked(p, save_err);
	if (out_file_saved) {
		out_msg += " Saved to sidecar file.";
	} else {
		out_msg += std::format(" Retained in-memory only (could not write sidecar: {}).", save_err);
	}

	return true;
}

void vcd_label_manager::clear_all()
{
	std::lock_guard<std::mutex> lock(mutex_);
	path_labels_.clear();
	path_qualifiers_.clear();
	file_loaded_.clear();
}

} // namespace verilog
