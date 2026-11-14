#include "vcd_label_manager.h"
#include "plugins/verilog/verilog_module_scanner.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <format>
#include <set>
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
		// If pattern is '*<suffix>' where <suffix> starts with an identifier char and has no other wildcards
		// (e.g. '*alu_out', '*opcode', '*sequencer.state'), match on a hierarchy '.' boundary rather than
		// inside a longer identifier like 'reg_alu_out'.
		if (pattern.starts_with('*') && pattern.size() > 1 && pattern[1] != '.' && pattern[1] != '_' &&
		    pattern.find('*', 1) == std::string_view::npos && pattern.find('?', 1) == std::string_view::npos) {
			std::string_view suffix = pattern.substr(1);
			if (signal_name == suffix ||
			    (signal_name.size() > suffix.size() &&
			     signal_name.ends_with(suffix) &&
			     signal_name[signal_name.size() - suffix.size() - 1] == '.')) {
				return true;
			}
			return false;
		}
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

static std::string canonicalize_path_key(std::string_view safe_path)
{
	if (safe_path.empty()) {
		return "";
	}
	std::error_code ec;
	auto canon = std::filesystem::weakly_canonical(std::filesystem::path(safe_path), ec);
	if (!ec && !canon.empty()) {
		return canon.string();
	}
	return std::string(safe_path);
}

std::string vcd_label_manager::get_global_sidecar_path(std::string_view safe_path)
{
	std::string norm_path = canonicalize_path_key(safe_path);
	if (norm_path.ends_with("vcd_labels.json")) {
		return norm_path;
	}

	std::filesystem::path p(norm_path);
	std::filesystem::path dir = p.parent_path();
	if (dir.empty()) {
		dir = ".";
	}

	std::string dir_name = dir.filename().string();
	if (dir.has_parent_path()) {
		std::filesystem::path parent = dir.parent_path();
		std::error_code ec;
		if (dir_name == "sim" || dir_name == "tb" || dir_name == "testbench" ||
		    dir_name == "wave" || dir_name == "waves" || dir_name == "vcd" ||
		    dir_name == "build" || dir_name == "obj_dir" || dir_name == "out" ||
		    (!std::filesystem::exists(dir / "vcd_labels.json", ec) &&
		     (std::filesystem::exists(parent / "vcd_labels.json", ec) ||
		      std::filesystem::exists(parent / "turbostar.json", ec) ||
		      std::filesystem::exists(parent / "project.json", ec) ||
		      std::filesystem::exists(parent / "src", ec) ||
		      std::filesystem::exists(parent / "sv", ec) ||
		      std::filesystem::exists(parent / "rtl", ec)))) {
			return canonicalize_path_key((parent / "vcd_labels.json").string());
		}
	}
	return canonicalize_path_key((dir / "vcd_labels.json").string());
}

void vcd_label_manager::load_json_file_locked(std::string_view json_path, std::string_view map_key)
{
	std::string key(map_key);
	std::string fpath(json_path);
	std::error_code ec;
	if (!std::filesystem::exists(fpath, ec)) {
		json_file_stamps_.erase(key);
		return;
	}

	file_stamp st;
	st.path = fpath;
	st.is_dir = false;
	st.mtime = std::filesystem::last_write_time(fpath, ec);
	st.size = std::filesystem::file_size(fpath, ec);
	json_file_stamps_[key] = st;

	std::ifstream file(fpath);
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
									path_labels_[key][sig][norm] = label_str.get<std::string>();
								}
							}
						}
					}
				}
				if (j.contains("qualifiers") && j["qualifiers"].is_object()) {
					for (auto &[sig, q_str] : j["qualifiers"].items()) {
						if (q_str.is_string()) {
							path_qualifiers_[key][sig] = q_str.get<std::string>();
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
								path_labels_[key][sig][norm] = label_str.get<std::string>();
							}
						}
					}
				}
			}
		}
	} catch (...) {}
}

void vcd_label_manager::reconcile_enum_labels_locked(std::string_view global_key, std::string_view target_key)
{
	std::string g_key(global_key);
	std::string t_key(target_key);
	auto it_enums = auto_enum_labels_.find(g_key);
	if (it_enums == auto_enum_labels_.end() || it_enums->second.empty()) {
		return;
	}
	auto it_target = path_labels_.find(t_key);
	if (it_target == path_labels_.end() || it_target->second.empty()) {
		return;
	}

	for (const auto &[enum_sig, enum_val_map] : it_enums->second) {
		if (enum_val_map.empty()) {
			continue;
		}
		std::set<std::string> enum_names;
		for (const auto &[_, lbl] : enum_val_map) {
			enum_names.insert(lbl);
		}

		for (auto &[user_sig, user_val_map] : it_target->second) {
			if (!signal_pattern_matches(enum_sig, user_sig) && !signal_pattern_matches(user_sig, enum_sig)) {
				continue;
			}
			// Check whether user_val_map contains label names belonging to this RTL enum
			bool overlaps_enum_names = false;
			for (const auto &[_, user_lbl] : user_val_map) {
				if (enum_names.contains(user_lbl)) {
					overlaps_enum_names = true;
					break;
				}
			}
			if (!overlaps_enum_names) {
				continue;
			}

			// Remove stale keys for enum constant names and apply the current RTL enum mapping
			for (auto it = user_val_map.begin(); it != user_val_map.end(); ) {
				if (enum_names.contains(it->second)) {
					it = user_val_map.erase(it);
				} else {
					++it;
				}
			}
			for (const auto &[hex_k, lbl] : enum_val_map) {
				user_val_map[hex_k] = lbl;
			}
		}
	}
}

void vcd_label_manager::load_sidecar_locked(std::string_view safe_path)
{
	std::string p = canonicalize_path_key(safe_path);
	std::string global_key = get_global_sidecar_path(p);

	auto is_json_stamp_stale = [&](const std::string &key, const std::string &fpath) -> bool {
		std::error_code ec;
		bool exists = std::filesystem::exists(fpath, ec);
		auto it = json_file_stamps_.find(key);
		if (!exists) {
			return it != json_file_stamps_.end();
		}
		if (it == json_file_stamps_.end()) {
			return true;
		}
		auto mtime = std::filesystem::last_write_time(fpath, ec);
		auto size = std::filesystem::file_size(fpath, ec);
		return ec || mtime != it->second.mtime || size != it->second.size;
	};

	if (!file_loaded_[global_key] || is_json_stamp_stale(global_key, global_key)) {
		if (file_loaded_[global_key]) {
			path_labels_[global_key].clear();
			path_qualifiers_[global_key].clear();
		}
		file_loaded_[global_key] = true;
		load_json_file_locked(global_key, global_key);
	}

	auto are_rtl_stamps_valid = [](const std::vector<file_stamp> &stamps) -> bool {
		if (stamps.empty()) {
			return false;
		}
		std::error_code ec;
		for (const auto &st : stamps) {
			auto mtime = std::filesystem::last_write_time(st.path, ec);
			if (ec || mtime != st.mtime) {
				return false;
			}
			if (!st.is_dir) {
				auto sz = std::filesystem::file_size(st.path, ec);
				if (ec || sz != st.size) {
					return false;
				}
			}
		}
		return true;
	};

	auto collect_rtl_stamps = [](const std::filesystem::path &project_dir) -> std::vector<file_stamp> {
		std::vector<file_stamp> stamps;
		std::error_code ec;
		if (!std::filesystem::exists(project_dir, ec)) {
			return stamps;
		}
		stamps.push_back({project_dir.string(), true, std::filesystem::last_write_time(project_dir, ec), 0});
		if (std::filesystem::is_directory(project_dir, ec)) {
			std::filesystem::recursive_directory_iterator it(
				project_dir, std::filesystem::directory_options::skip_permission_denied, ec);
			std::filesystem::recursive_directory_iterator end;
			while (!ec && it != end) {
				const auto &ep = it->path();
				std::string fname = ep.filename().string();
				if (it->is_directory(ec)) {
					if (fname.starts_with('.') || fname == "build" || fname == "obj_dir" || fname == "templates") {
						it.disable_recursion_pending();
					} else {
						stamps.push_back({ep.string(), true, it->last_write_time(ec), 0});
					}
				} else if (it->is_regular_file(ec) && !fname.starts_with('.') && !fname.ends_with('~')) {
					auto ext = ep.extension().string();
					if (ext == ".sv" || ext == ".v" || ext == ".svh" || ext == ".vh") {
						stamps.push_back({ep.string(), false, it->last_write_time(ec), it->file_size(ec)});
					}
				}
				it.increment(ec);
			}
		}
		return stamps;
	};

	if (!auto_enum_loaded_[global_key] || !are_rtl_stamps_valid(auto_enum_stamps_[global_key])) {
		auto_enum_loaded_[global_key] = true;
		std::filesystem::path project_dir = std::filesystem::path(global_key).parent_path();
		if (project_dir.empty()) {
			project_dir = ".";
		}
		auto_enum_stamps_[global_key] = collect_rtl_stamps(project_dir);
		auto_enum_labels_[global_key] = verilog_module_scanner::discover_enum_signal_labels(project_dir);
		reconcile_enum_labels_locked(global_key, global_key);
		if (p != global_key && file_loaded_[p]) {
			reconcile_enum_labels_locked(global_key, p);
		}
	}

	if (p == global_key) {
		return;
	}

	std::string sidecar_path = p + ".labels.json";
	if (!std::filesystem::exists(sidecar_path)) {
		if (p.ends_with(".previous.vcd") || p.ends_with(".lastpass.vcd")) {
			std::string base_sidecar = p.substr(0, p.size() - 13) + ".vcd.labels.json";
			if (std::filesystem::exists(base_sidecar)) {
				sidecar_path = base_sidecar;
			}
		}
	}

	if (!file_loaded_[p] || is_json_stamp_stale(p, sidecar_path)) {
		if (file_loaded_[p] && std::filesystem::exists(sidecar_path)) {
			path_labels_[p].clear();
			path_qualifiers_[p].clear();
		}
		file_loaded_[p] = true;
		if (std::filesystem::exists(sidecar_path)) {
			load_json_file_locked(sidecar_path, p);
			reconcile_enum_labels_locked(global_key, p);
		}
	}
}

bool vcd_label_manager::save_sidecar_locked(std::string_view target_key, std::string &out_error)
{
	std::string p = canonicalize_path_key(target_key);
	std::string sidecar_path = p.ends_with("vcd_labels.json") ? p : (p + ".labels.json");

	const auto &sig_map = path_labels_[p];
	const auto &qual_map = path_qualifiers_[p];
	if (sig_map.empty() && qual_map.empty()) {
		// If both maps are completely empty, remove sidecar file if it exists
		if (std::filesystem::exists(sidecar_path)) {
			std::error_code ec;
			std::filesystem::remove(sidecar_path, ec);
		}
		json_file_stamps_.erase(p);
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
	file.close();

	std::error_code ec;
	file_stamp st;
	st.path = sidecar_path;
	st.is_dir = false;
	st.mtime = std::filesystem::last_write_time(sidecar_path, ec);
	st.size = std::filesystem::file_size(sidecar_path, ec);
	json_file_stamps_[p] = st;
	return true;
}

std::map<std::string, std::map<std::string, std::string>> vcd_label_manager::merged_labels_locked(std::string_view safe_path)
{
	std::string p = canonicalize_path_key(safe_path);
	load_sidecar_locked(p);
	std::string global_key = get_global_sidecar_path(p);

	auto merged = auto_enum_labels_[global_key];
	for (const auto &[sig, val_map] : path_labels_[global_key]) {
		for (const auto &[k, v] : val_map) {
			merged[sig][k] = v;
		}
	}
	if (p != global_key) {
		for (const auto &[sig, val_map] : path_labels_[p]) {
			for (const auto &[k, v] : val_map) {
				merged[sig][k] = v;
			}
		}
	}
	return merged;
}

std::map<std::string, std::string> vcd_label_manager::merged_qualifiers_locked(std::string_view safe_path)
{
	std::string p = canonicalize_path_key(safe_path);
	load_sidecar_locked(p);
	std::string global_key = get_global_sidecar_path(p);

	auto merged = path_qualifiers_[global_key];
	if (p != global_key) {
		for (const auto &[sig, q] : path_qualifiers_[p]) {
			merged[sig] = q;
		}
	}
	return merged;
}

std::map<std::string, std::map<std::string, std::string>> vcd_label_manager::get_labels_for_file(std::string_view safe_path)
{
	std::lock_guard<std::mutex> lock(mutex_);
	return merged_labels_locked(safe_path);
}

std::string vcd_label_manager::find_label(
	std::string_view safe_path,
	std::string_view signal_name,
	std::string_view hex_val,
	std::string_view raw_bin_val
)
{
	std::lock_guard<std::mutex> lock(mutex_);
	std::string p = canonicalize_path_key(safe_path);
	load_sidecar_locked(p);
	std::string global_key = get_global_sidecar_path(p);
	std::string norm_hex = normalize_hex_key(hex_val);

	auto strip_hex_zeros = [](std::string_view h) -> std::string_view {
		if (!h.starts_with("0x")) {
			return {};
		}
		std::string_view digits = h.substr(2);
		while (digits.size() > 1 && digits.front() == '0') {
			digits.remove_prefix(1);
		}
		return digits;
	};
	std::string_view norm_hex_stripped = strip_hex_zeros(norm_hex);

	// Helper to search a specific signal's val_map
	auto match_in_val_map = [&](const std::map<std::string, std::string> &val_map) -> std::string {
		// 1. Direct match on normalized hex key
		auto it = val_map.find(norm_hex);
		if (it != val_map.end()) {
			return it->second;
		}

		// 1b. Match on hex digits ignoring leading zeros (e.g. "0x0" vs "0x00", "0x6" vs "0x06")
		if (!norm_hex_stripped.empty()) {
			for (const auto &[k, label] : val_map) {
				if (strip_hex_zeros(k) == norm_hex_stripped) {
					return label;
				}
			}
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

	auto search_sig_map = [&](const std::map<std::string, std::map<std::string, std::string>> &sig_map) -> std::string {
		if (sig_map.empty()) {
			return "";
		}
		// 1. Try exact signal name match
		auto it = sig_map.find(std::string(signal_name));
		if (it != sig_map.end()) {
			std::string res = match_in_val_map(it->second);
			if (!res.empty()) return res;
		}

		// 2. Try matching by leaf short name, suffix, or glob wildcard pattern (e.g. "module1.*.opcode" or "*opcode"),
		// preferring more specific patterns (more non-wildcard characters)
		std::string best_label;
		size_t best_specificity = 0;
		bool found_any = false;
		for (const auto &[reg_sig, val_map] : sig_map) {
			if (reg_sig == signal_name) {
				continue; // Already checked exact match
			}
			if (signal_pattern_matches(reg_sig, signal_name)) {
				std::string res = match_in_val_map(val_map);
				if (!res.empty()) {
					size_t spec = 0;
					for (char c : reg_sig) {
						if (c != '*' && c != '?') spec++;
					}
					if (!found_any || spec > best_specificity) {
						best_label = std::move(res);
						best_specificity = spec;
						found_any = true;
					}
				}
			}
		}
		return best_label;
	};

	// Check per-file local overrides first, then project-global vcd_labels.json, then auto-discovered SV enums
	if (p != global_key) {
		std::string local_res = search_sig_map(path_labels_[p]);
		if (!local_res.empty()) {
			return local_res;
		}
	}
	std::string global_res = search_sig_map(path_labels_[global_key]);
	if (!global_res.empty()) {
		return global_res;
	}
	return search_sig_map(auto_enum_labels_[global_key]);
}

bool vcd_label_manager::set_labels(
	std::string_view safe_path,
	std::string_view signal_name,
	const std::map<std::string, std::string> &val_to_label,
	bool clear,
	bool &out_file_saved,
	std::string &out_msg,
	bool is_global
)
{
	std::lock_guard<std::mutex> lock(mutex_);
	std::string p = canonicalize_path_key(safe_path);
	load_sidecar_locked(p);
	std::string target_key = is_global ? get_global_sidecar_path(p) : p;

	if (clear) {
		if (signal_name.empty()) {
			path_labels_[target_key].clear();
			if (is_global && p != target_key) {
				path_labels_[p].clear();
				std::string dummy_err;
				save_sidecar_locked(p, dummy_err);
			}
			out_msg = is_global
				? "Cleared all signal labels in project-global vcd_labels.json."
				: "Cleared all signal labels for this VCD file.";
		} else {
			path_labels_[target_key].erase(std::string(signal_name));
			if (is_global && p != target_key) {
				path_labels_[p].erase(std::string(signal_name));
				std::string dummy_err;
				save_sidecar_locked(p, dummy_err);
			}
			out_msg = std::format("Cleared labels for signal '{}'.", signal_name);
		}
	} else {
		std::map<std::string, std::string> norm_updates;
		for (const auto &[val_raw, label] : val_to_label) {
			std::string norm = normalize_hex_key(val_raw);
			path_labels_[target_key][std::string(signal_name)][norm] = label;
			norm_updates[norm] = label;
		}

		// If signal_name overlaps with an existing pattern (e.g. "*opcode*" vs "*opcode" or "opcode"),
		// propagate conflicting hex key updates so the older pattern does not shadow the newer mapping.
		size_t updated_overlapping = 0;
		for (auto &[existing_sig, existing_map] : path_labels_[target_key]) {
			if (existing_sig == signal_name) continue;
			if (signal_pattern_matches(signal_name, existing_sig) || signal_pattern_matches(existing_sig, signal_name)) {
				bool touched = false;
				for (const auto &[norm_k, new_lbl] : norm_updates) {
					if (existing_map.contains(norm_k)) {
						existing_map[norm_k] = new_lbl;
						touched = true;
					}
				}
				if (touched) {
					updated_overlapping++;
				}
			}
		}

		out_msg = std::format("Registered {} label(s) for signal '{}'.", val_to_label.size(), signal_name);
		if (updated_overlapping > 0) {
			out_msg += std::format(" Updated {} overlapping pattern(s).", updated_overlapping);
		}
	}

	// Attempt sidecar save
	std::string save_err;
	out_file_saved = save_sidecar_locked(target_key, save_err);
	if (out_file_saved) {
		out_msg += is_global ? " Saved to project-global vcd_labels.json." : " Saved to sidecar file.";
	} else {
		out_msg += std::format(" Retained in-memory only (could not write sidecar: {}).", save_err);
	}

	return true;
}

std::map<std::string, std::string> vcd_label_manager::get_qualifiers_for_file(std::string_view safe_path)
{
	std::lock_guard<std::mutex> lock(mutex_);
	return merged_qualifiers_locked(safe_path);
}

std::pair<std::string, bool> vcd_label_manager::find_qualifier(std::string_view safe_path, std::string_view signal_name)
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (signal_name.empty()) {
		return {"", false};
	}
	std::string p = canonicalize_path_key(safe_path);
	load_sidecar_locked(p);
	std::string global_key = get_global_sidecar_path(p);

	auto parse_expr = [](std::string_view expr) -> std::pair<std::string, bool> {
		bool active_low = false;
		std::string s(expr);
		if (s.starts_with('~') || s.starts_with('!')) {
			active_low = true;
			s.erase(0, 1);
		}
		return {s, active_low};
	};

	auto search_qual_map = [&](const std::map<std::string, std::string> &qual_map) -> std::pair<std::string, bool> {
		if (qual_map.empty()) {
			return {"", false};
		}
		// 1. Exact match
		auto it = qual_map.find(std::string(signal_name));
		if (it != qual_map.end()) {
			return parse_expr(it->second);
		}
		// 2. Non-wildcard suffix/leaf match (longest/most-specific pattern first, e.g. "alu.alu_out" before "alu_out")
		const std::string *best_nonwild_expr = nullptr;
		size_t best_nonwild_len = 0;
		for (const auto &[reg_sig, q_expr] : qual_map) {
			if (reg_sig.find('*') == std::string::npos && reg_sig.find('?') == std::string::npos) {
				if (signal_pattern_matches(reg_sig, signal_name) && reg_sig.size() > best_nonwild_len) {
					best_nonwild_len = reg_sig.size();
					best_nonwild_expr = &q_expr;
				}
			}
		}
		if (best_nonwild_expr) {
			return parse_expr(*best_nonwild_expr);
		}
		// 3. Wildcard pattern match (longest pattern first)
		const std::string *best_wild_expr = nullptr;
		size_t best_wild_len = 0;
		for (const auto &[reg_sig, q_expr] : qual_map) {
			if (signal_pattern_matches(reg_sig, signal_name) && reg_sig.size() > best_wild_len) {
				best_wild_len = reg_sig.size();
				best_wild_expr = &q_expr;
			}
		}
		if (best_wild_expr) {
			return parse_expr(*best_wild_expr);
		}
		return {"", false};
	};

	if (p != global_key) {
		auto local_res = search_qual_map(path_qualifiers_[p]);
		if (!local_res.first.empty()) {
			return local_res;
		}
	}
	return search_qual_map(path_qualifiers_[global_key]);
}
bool vcd_label_manager::set_qualifier(
	std::string_view safe_path,
	std::string_view signal_name,
	std::string_view qualifier_expr,
	bool clear,
	bool &out_file_saved,
	std::string &out_msg,
	bool is_global
)
{
	std::lock_guard<std::mutex> lock(mutex_);
	std::string p = canonicalize_path_key(safe_path);
	load_sidecar_locked(p);
	std::string target_key = is_global ? get_global_sidecar_path(p) : p;

	if (clear) {
		if (signal_name.empty()) {
			path_qualifiers_[target_key].clear();
			if (is_global && p != target_key) {
				path_qualifiers_[p].clear();
				std::string dummy_err;
				save_sidecar_locked(p, dummy_err);
			}
			out_msg = is_global
				? "Cleared all signal qualifiers in project-global vcd_labels.json."
				: "Cleared all signal qualifiers for this VCD file.";
		} else {
			path_qualifiers_[target_key].erase(std::string(signal_name));
			if (is_global && p != target_key) {
				path_qualifiers_[p].erase(std::string(signal_name));
				std::string dummy_err;
				save_sidecar_locked(p, dummy_err);
			}
			out_msg = std::format("Cleared qualifier for signal '{}'.", signal_name);
		}
	} else {
		path_qualifiers_[target_key][std::string(signal_name)] = std::string(qualifier_expr);
		out_msg = std::format("Registered qualifier '{}' for signal '{}'.", qualifier_expr, signal_name);
	}

	std::string save_err;
	out_file_saved = save_sidecar_locked(target_key, save_err);
	if (out_file_saved) {
		out_msg += is_global ? " Saved to project-global vcd_labels.json." : " Saved to sidecar file.";
	} else {
		out_msg += std::format(" Retained in-memory only (could not write sidecar: {}).", save_err);
	}

	return true;
}

bool vcd_label_manager::set_qualifiers_batch(
	std::string_view safe_path,
	const std::map<std::string, std::string> &bindings,
	bool &out_file_saved,
	std::string &out_msg,
	bool is_global
)
{
	std::lock_guard<std::mutex> lock(mutex_);
	std::string p = canonicalize_path_key(safe_path);
	load_sidecar_locked(p);
	std::string target_key = is_global ? get_global_sidecar_path(p) : p;

	for (const auto &[sig, q] : bindings) {
		path_qualifiers_[target_key][sig] = q;
	}
	out_msg = std::format("Registered {} signal qualifier binding(s).", bindings.size());

	std::string save_err;
	out_file_saved = save_sidecar_locked(target_key, save_err);
	if (out_file_saved) {
		out_msg += is_global ? " Saved to project-global vcd_labels.json." : " Saved to sidecar file.";
	} else {
		out_msg += std::format(" Retained in-memory only (could not write sidecar: {}).", save_err);
	}

	return true;
}

bool vcd_label_manager::has_labels_for_signal(std::string_view safe_path, std::string_view signal_name)
{
	std::lock_guard<std::mutex> lock(mutex_);
	auto sig_map = merged_labels_locked(safe_path);
	if (sig_map.empty() || signal_name.empty()) {
		return false;
	}

	auto it = sig_map.find(std::string(signal_name));
	if (it != sig_map.end() && !it->second.empty()) {
		return true;
	}

	for (const auto &[reg_sig, val_map] : sig_map) {
		if (!val_map.empty() && signal_pattern_matches(reg_sig, signal_name)) {
			return true;
		}
	}
	return false;
}

bool vcd_label_manager::try_consume_qualifier_hint() noexcept
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (qualifier_hint_shown_) {
		return false;
	}
	qualifier_hint_shown_ = true;
	return true;
}

bool vcd_label_manager::try_consume_label_hint() noexcept
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (label_hint_shown_) {
		return false;
	}
	label_hint_shown_ = true;
	return true;
}

void vcd_label_manager::clear_all()
{
	std::lock_guard<std::mutex> lock(mutex_);
	path_labels_.clear();
	path_qualifiers_.clear();
	auto_enum_labels_.clear();
	file_loaded_.clear();
	json_file_stamps_.clear();
	auto_enum_loaded_.clear();
	auto_enum_stamps_.clear();
	qualifier_hint_shown_ = false;
	label_hint_shown_ = false;
}

void vcd_label_manager::invalidate_auto_enums()
{
	std::lock_guard<std::mutex> lock(mutex_);
	auto_enum_loaded_.clear();
	auto_enum_stamps_.clear();
}
} // namespace verilog
