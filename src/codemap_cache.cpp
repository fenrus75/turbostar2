#include "codemap_cache.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include <system_error>
#include <unistd.h>
#include "event_logger.h"
#include "fs_utils.h"
#include "type_definition_cache.h"

namespace tools
{

codemap_cache &codemap_cache::get_instance()
{
	static codemap_cache instance;
	return instance;
}

codemap_cache::codemap_cache() = default;

codemap_cache::~codemap_cache()
{
	save_to_disk();
}

void codemap_cache::initialize(const std::string &project_root)
{
	std::lock_guard<std::mutex> lock(cache_mutex_);
	project_root_ = project_root;
	loaded_.store(false, std::memory_order_release);
}

void codemap_cache::shutdown()
{
	save_to_disk();
}

bool codemap_cache::is_loaded() const
{
	return loaded_.load(std::memory_order_acquire);
}

void codemap_cache::ensure_loaded() const
{
	if (loaded_.load(std::memory_order_acquire)) {
		return;
	}
	std::lock_guard<std::mutex> lock(load_mutex_);
	if (loaded_.load(std::memory_order_relaxed)) {
		return;
	}
	const_cast<codemap_cache *>(this)->load_from_disk();
}

std::string codemap_cache::resolve_cache_filepath_locked() const
{
	if (!cache_file_override_.empty()) {
		return cache_file_override_;
	}
	std::string root = fs_utils::get_project_cache_root();
	if (root.empty()) {
		return "";
	}
	return (std::filesystem::path(root) / "codemap_cache.json").string();
}

std::string codemap_cache::resolve_cache_filepath() const
{
	std::lock_guard<std::mutex> lock(cache_mutex_);
	return resolve_cache_filepath_locked();
}

void codemap_cache::set_cache_file_override(const std::string &filepath)
{
	std::lock_guard<std::mutex> lock(cache_mutex_);
	cache_file_override_ = filepath;
	loaded_.store(false, std::memory_order_release);
}

void codemap_cache::clear()
{
	std::lock_guard<std::mutex> lock(cache_mutex_);
	entries_.clear();
	dirty_ = false;
	loaded_.store(true, std::memory_order_release);
}

size_t codemap_cache::size() const
{
	ensure_loaded();
	std::lock_guard<std::mutex> lock(cache_mutex_);
	return entries_.size();
}

void codemap_cache::invalidate(const std::string &safe_path)
{
	std::lock_guard<std::mutex> lock(cache_mutex_);
	if (!loaded_.load(std::memory_order_relaxed)) {
		return;
	}
	auto it = entries_.find(safe_path);
	if (it != entries_.end()) {
		entries_.erase(it);
		dirty_ = true;
	}
}

void codemap_cache::prune_lru(size_t max_entries)
{
	ensure_loaded();

	std::lock_guard<std::mutex> lock(cache_mutex_);
	if (entries_.size() <= max_entries) {
		return;
	}

	std::vector<std::pair<std::string, int64_t>> usage;
	usage.reserve(entries_.size());
	for (const auto &[path, entry] : entries_) {
		usage.emplace_back(path, entry.last_used_ms);
	}

	std::sort(usage.begin(), usage.end(), [](const auto &a, const auto &b) { return a.second < b.second; });

	size_t to_remove = entries_.size() - max_entries;
	for (size_t i = 0; i < to_remove && i < usage.size(); ++i) {
		entries_.erase(usage[i].first);
	}
	dirty_ = true;
}

bool codemap_cache::get(const std::string &safe_path, int min_lines, std::vector<codemap_symbol_info> &out)
{
	std::error_code ec;
	auto current_mtime = std::filesystem::last_write_time(safe_path, ec);
	if (ec) {
		return false;
	}
	auto current_size = std::filesystem::file_size(safe_path, ec);
	if (ec) {
		return false;
	}

	ensure_loaded();

	std::lock_guard<std::mutex> lock(cache_mutex_);
	auto it = entries_.find(safe_path);
	if (it == entries_.end()) {
		return false;
	}

	auto current_sys_time = std::chrono::file_clock::to_sys(current_mtime);
	int64_t current_mtime_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(current_sys_time.time_since_epoch()).count();

	auto cached_sys_time = std::chrono::file_clock::to_sys(it->second.mtime);
	int64_t cached_mtime_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(cached_sys_time.time_since_epoch()).count();

	if (current_mtime_ns != cached_mtime_ns || current_size != it->second.file_size) {
		// File has changed since last cache update; invalidate
		entries_.erase(it);
		dirty_ = true;
		return false;
	}

	// Update last_used timestamp for LRU tracking
	it->second.last_used_ms =
	    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

	out.clear();
	if (min_lines <= 1) {
		out = it->second.symbols;
	} else {
		out.reserve(it->second.symbols.size());
		for (const auto &sym : it->second.symbols) {
			if (sym.line_count >= min_lines) {
				out.push_back(sym);
			}
		}
	}
	return true;
}

bool codemap_cache::has_valid_entry(const std::string &safe_path) const
{
	std::error_code ec;
	auto current_mtime = std::filesystem::last_write_time(safe_path, ec);
	if (ec) {
		return false;
	}
	auto current_size = std::filesystem::file_size(safe_path, ec);
	if (ec) {
		return false;
	}

	ensure_loaded();

	std::lock_guard<std::mutex> lock(cache_mutex_);
	auto it = entries_.find(safe_path);
	if (it == entries_.end()) {
		return false;
	}

	auto current_sys_time = std::chrono::file_clock::to_sys(current_mtime);
	int64_t current_mtime_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(current_sys_time.time_since_epoch()).count();

	auto cached_sys_time = std::chrono::file_clock::to_sys(it->second.mtime);
	int64_t cached_mtime_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(cached_sys_time.time_since_epoch()).count();

	return (current_mtime_ns == cached_mtime_ns && current_size == it->second.file_size);
}

void codemap_cache::put(const std::string &safe_path, std::vector<codemap_symbol_info> symbols)
{
	std::error_code ec;
	auto current_mtime = std::filesystem::last_write_time(safe_path, ec);
	if (ec) {
		return;
	}
	auto current_size = std::filesystem::file_size(safe_path, ec);
	if (ec) {
		return;
	}

	ensure_loaded();

	// Pre-warm type definition cache for classes, structs, enums
	for (const auto &sym : symbols) {
		if (sym.kind_str == "Class" || sym.kind_str == "Struct" || sym.kind_str == "Enum" || sym.kind_str == "class" ||
		    sym.kind_str == "struct" || sym.kind_str == "enum") {
			type_definition_cache::get_instance().register_resolved_type(sym.name, sym.kind_str, safe_path, sym.start_line,
										     sym.end_line);
		}
	}

	std::lock_guard<std::mutex> lock(cache_mutex_);
	cache_entry entry;
	entry.mtime = current_mtime;
	entry.file_size = current_size;
	entry.last_used_ms =
	    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	entry.symbols = std::move(symbols);

	entries_[safe_path] = std::move(entry);
	dirty_ = true;

	if (entries_.size() > max_entries_) {
		// Prune oldest 25% of entries
		std::vector<std::pair<std::string, int64_t>> usage;
		usage.reserve(entries_.size());
		for (const auto &[path, ent] : entries_) {
			usage.emplace_back(path, ent.last_used_ms);
		}
		std::sort(usage.begin(), usage.end(), [](const auto &a, const auto &b) { return a.second < b.second; });
		size_t to_remove = entries_.size() - (max_entries_ * 3 / 4);
		for (size_t i = 0; i < to_remove && i < usage.size(); ++i) {
			entries_.erase(usage[i].first);
		}
	}
}

void codemap_cache::load_from_disk()
{
	std::string cache_path = resolve_cache_filepath();
	if (cache_path.empty() || !std::filesystem::exists(cache_path)) {
		loaded_.store(true, std::memory_order_release);
		return;
	}

	std::ifstream in(cache_path);
	if (!in.is_open()) {
		loaded_.store(true, std::memory_order_release);
		return;
	}

	nlohmann::json j;
	try {
		in >> j;
	} catch (const std::exception &ex) {
		event_logger::get_instance().log_warn("codemap_cache: failed to parse cache file '{}': {}", cache_path, ex.what());
		loaded_.store(true, std::memory_order_release);
		return;
	}

	std::vector<std::string> stale_or_changed_files;
	std::unordered_map<std::string, cache_entry> loaded_entries;

	if (j.contains("files") && j["files"].is_object()) {
		for (auto it = j["files"].begin(); it != j["files"].end(); ++it) {
			const std::string &filepath = it.key();
			const auto &val = it.value();

			std::error_code ec;
			auto current_mtime = std::filesystem::last_write_time(filepath, ec);
			if (ec) {
				// File was removed or cannot be accessed; prune from cache
				continue;
			}
			auto current_size = std::filesystem::file_size(filepath, ec);
			if (ec) {
				continue;
			}

			int64_t saved_mtime_ns = val.value("mtime_ns", 0LL);
			uint64_t saved_size = val.value("size", 0ULL);
			int64_t last_used_ms = val.value("last_used_ms", 0LL);

			auto current_sys_time = std::chrono::file_clock::to_sys(current_mtime);
			int64_t current_mtime_ns =
			    std::chrono::duration_cast<std::chrono::nanoseconds>(current_sys_time.time_since_epoch()).count();

			// Compare mtime and size against current disk state
			if (current_mtime_ns != saved_mtime_ns || current_size != saved_size) {
				// File modified since cache was written; queue for background prefetch
				stale_or_changed_files.push_back(filepath);
				continue;
			}

			// Valid entry! Deserialize symbols
			cache_entry entry;
			entry.mtime = current_mtime;
			entry.file_size = current_size;
			entry.last_used_ms =
			    last_used_ms > 0
				? last_used_ms
				: std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
				      .count();

			if (val.contains("symbols") && val["symbols"].is_array()) {
				for (const auto &sym_j : val["symbols"]) {
					codemap_symbol_info sym;
					sym.name = sym_j.value("name", "");
					sym.display_name = sym_j.value("display", sym.name);
					sym.kind_str = sym_j.value("kind", "");
					sym.start_line = sym_j.value("start", 0);
					sym.end_line = sym_j.value("end", 0);
					sym.line_count = sym_j.value("lines", std::max(1, sym.end_line - sym.start_line + 1));
					sym.depth = sym_j.value("depth", 0);
					sym.source_file = sym_j.value("source", "");
					// Pre-warm type definition cache for classes, structs, enums
					if (sym.kind_str == "Class" || sym.kind_str == "Struct" || sym.kind_str == "Enum" ||
					    sym.kind_str == "class" || sym.kind_str == "struct" || sym.kind_str == "enum") {
						type_definition_cache::get_instance().register_resolved_type(
						    sym.name, sym.kind_str, filepath, sym.start_line, sym.end_line);
					}

					entry.symbols.push_back(std::move(sym));
				}
			}

			loaded_entries[filepath] = std::move(entry);
		}
	}

	size_t loaded_count = loaded_entries.size();
	{
		std::lock_guard<std::mutex> lock(cache_mutex_);
		entries_ = std::move(loaded_entries);
		dirty_ = false;
		loaded_.store(true, std::memory_order_release);
	}

	event_logger::get_instance().log_info("codemap_cache: loaded {} valid entries from disk ({} stale files queued for refresh)",
					      loaded_count, stale_or_changed_files.size());

	// Kick off asynchronous prefetch for all changed/stale files so caches are warm when queries arrive
	for (const auto &changed_path : stale_or_changed_files) {
		prefetch_document_codemap_symbols(changed_path);
	}
}

void codemap_cache::save_to_disk()
{
	std::unordered_map<std::string, cache_entry> snapshot;
	std::string cache_path;
	{
		std::lock_guard<std::mutex> lock(cache_mutex_);
		if (!dirty_ || !loaded_.load(std::memory_order_relaxed)) {
			return;
		}
		snapshot = entries_;
		cache_path = resolve_cache_filepath_locked();
		dirty_ = false;
	}

	if (cache_path.empty()) {
		return;
	}

	nlohmann::json j;
	j["version"] = 1;
	nlohmann::json files_j = nlohmann::json::object();

	for (const auto &[path, entry] : snapshot) {
		nlohmann::json f;
		auto sys_time = std::chrono::file_clock::to_sys(entry.mtime);
		int64_t mtime_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(sys_time.time_since_epoch()).count();
		f["mtime_ns"] = mtime_ns;
		f["size"] = entry.file_size;
		f["last_used_ms"] = entry.last_used_ms;

		nlohmann::json syms = nlohmann::json::array();
		for (const auto &sym : entry.symbols) {
			nlohmann::json s;
			s["name"] = sym.name;
			if (sym.display_name != sym.name) {
				s["display"] = sym.display_name;
			}
			s["kind"] = sym.kind_str;
			s["start"] = sym.start_line;
			s["end"] = sym.end_line;
			s["lines"] = sym.line_count;
			if (sym.depth > 0) {
				s["depth"] = sym.depth;
			}
			if (!sym.source_file.empty()) {
				s["source"] = sym.source_file;
			}
			syms.push_back(std::move(s));
		}
		f["symbols"] = std::move(syms);
		files_j[path] = std::move(f);
	}

	j["files"] = std::move(files_j);

	std::string tmp_dir = fs_utils::get_project_tmp_dir();
	std::string tmp_file = (std::filesystem::path(tmp_dir) / std::format("codemap_cache_{}.tmp", static_cast<long>(getpid()))).string();

	std::ofstream out(tmp_file);
	if (out.is_open()) {
		out << j.dump();
		out.close();

		std::error_code ec;
		std::filesystem::rename(tmp_file, cache_path, ec);
		if (ec) {
			std::filesystem::copy_file(tmp_file, cache_path, std::filesystem::copy_options::overwrite_existing, ec);
			std::filesystem::remove(tmp_file, ec);
		}
	}
}

} // namespace tools
