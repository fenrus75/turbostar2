#include "type_definition_cache.h"
#include <algorithm>
#include <filesystem>
#include <format>
#include <thread>
#include "codemap_utils.h"
#include "event_logger.h"
#include "fs_utils.h"
#include "project_manager.h"

namespace tools
{

type_definition_cache &type_definition_cache::get_instance()
{
	static type_definition_cache instance;
	return instance;
}

std::optional<type_definition_entry> type_definition_cache::lookup(std::string_view type_name)
{
	std::lock_guard<std::mutex> lock(mutex_);
	auto it = types_.find(std::string(type_name));
	if (it != types_.end()) {
		return it->second;
	}
	return std::nullopt;
}

bool type_definition_cache::contains(std::string_view type_name)
{
	std::lock_guard<std::mutex> lock(mutex_);
	return types_.find(std::string(type_name)) != types_.end();
}

void type_definition_cache::register_resolved_type(std::string_view type_name, std::string_view kind, std::string_view safe_path,
						   int start_line, int end_line, std::string_view underlying_type)
{
	if (type_name.empty() || safe_path.empty()) {
		return;
	}

	std::lock_guard<std::mutex> lock(mutex_);
	type_definition_entry entry;
	entry.type_name = std::string(type_name);
	entry.state = type_cache_state::resolved;
	entry.safe_file_path = std::string(safe_path);
	entry.kind = std::string(kind);
	entry.underlying_type = std::string(underlying_type);
	entry.start_line = start_line;
	entry.end_line = end_line;
	entry.requested_at = std::chrono::steady_clock::now();

	types_[std::string(type_name)] = std::move(entry);
}

static void extract_typedef_or_alias(std::string_view line, std::string_view type_name, std::string &kind, std::string &underlying_type)
{
	// 1. Check for C++ type alias: using <type_name> = <underlying>;
	size_t using_pos = line.find("using");
	if (using_pos != std::string_view::npos) {
		size_t after_using = using_pos + 5;
		if (after_using < line.size() && (line[after_using] == ' ' || line[after_using] == '\t')) {
			size_t name_pos = line.find(type_name, after_using);
			if (name_pos != std::string_view::npos) {
				size_t eq_pos = line.find('=', name_pos + type_name.size());
				if (eq_pos != std::string_view::npos) {
					std::string_view rhs = line.substr(eq_pos + 1);
					size_t semi = rhs.find(';');
					if (semi != std::string_view::npos) {
						rhs = rhs.substr(0, semi);
					}
					size_t first = rhs.find_first_not_of(" \t");
					size_t last = rhs.find_last_not_of(" \t\r\n");
					if (first != std::string_view::npos && last != std::string_view::npos && first <= last) {
						kind = "typedef";
						underlying_type = std::string(rhs.substr(first, last - first + 1));
						return;
					}
				}
			}
		}
	}

	// 2. Check for C/C++ typedef: typedef <underlying> <type_name>;
	size_t td_pos = line.find("typedef");
	if (td_pos != std::string_view::npos) {
		size_t after_td = td_pos + 7;
		if (after_td < line.size() && (line[after_td] == ' ' || line[after_td] == '\t')) {
			size_t semi = line.find(';', after_td);
			std::string_view stmt =
			    (semi != std::string_view::npos) ? line.substr(after_td, semi - after_td) : line.substr(after_td);

			size_t name_pos = stmt.rfind(type_name);
			if (name_pos != std::string_view::npos) {
				bool start_ok = (name_pos == 0) || (!std::isalnum(static_cast<unsigned char>(stmt[name_pos - 1])) &&
								    stmt[name_pos - 1] != '_');
				bool end_ok = (name_pos + type_name.size() == stmt.size()) ||
					      (!std::isalnum(static_cast<unsigned char>(stmt[name_pos + type_name.size()])) &&
					       stmt[name_pos + type_name.size()] != '_');
				if (start_ok && end_ok) {
					std::string_view underlying = stmt.substr(0, name_pos);
					size_t first = underlying.find_first_not_of(" \t");
					size_t last = underlying.find_last_not_of(" \t\r\n");
					if (first != std::string_view::npos && last != std::string_view::npos && first <= last) {
						kind = "typedef";
						underlying_type = std::string(underlying.substr(first, last - first + 1));
						return;
					}
				}
			}
		}
	}
}

void type_definition_cache::request_async(std::string_view type_name, const std::string &referencing_file, int line, int character)
{
	if (type_name.empty() || referencing_file.empty()) {
		return;
	}

	std::string t_name(type_name);
	{
		std::lock_guard<std::mutex> lock(mutex_);
		auto it = types_.find(t_name);
		if (it != types_.end()) {
			event_logger::get_instance().log(
			    std::format("type_definition_cache::request_async: type='{}' already tracked (state={})", t_name,
					static_cast<int>(it->second.state)));
			return;
		}

		type_definition_entry pending_stub;
		pending_stub.type_name = t_name;
		pending_stub.state = type_cache_state::pending;
		pending_stub.requested_at = std::chrono::steady_clock::now();
		types_[t_name] = pending_stub;
		event_logger::get_instance().log(
		    std::format("type_definition_cache::request_async: dispatched background worker for type='{}' (ref='{}:{}:{}')", t_name,
				referencing_file, line, character));
	}

	// Dispatch non-blocking background resolution worker
	std::thread([this, t_name, referencing_file, line, character]() {
		fs_utils::set_current_thread_name("type_cache");
		try {
			if (!project_manager::get_instance().lsp_is_supported_file(referencing_file)) {
				std::lock_guard<std::mutex> lock(mutex_);
				types_[t_name].state = type_cache_state::unresolved;
				event_logger::get_instance().log(
				    std::format("type_definition_cache: type='{}', file='{}' not supported by LSP -> unresolved", t_name,
						referencing_file));
				return;
			}

			auto locs = project_manager::get_instance().lsp_query_type_definition(referencing_file, line, character);
			if (locs.empty()) {
				std::lock_guard<std::mutex> lock(mutex_);
				types_[t_name].state = type_cache_state::unresolved;
				event_logger::get_instance().log(
				    std::format("type_definition_cache: type='{}', no definition locations found -> unresolved", t_name));
				return;
			}

			const auto &loc = locs[0];
			std::string proj_root = project_manager::get_instance().get_project_root();
			std::string abs_path = std::filesystem::absolute(loc.path).lexically_normal().string();
			std::string norm_root = std::filesystem::absolute(proj_root).lexically_normal().string();

			// Check if file is within project workspace
			if (abs_path.find(norm_root) != 0) {
				std::lock_guard<std::mutex> lock(mutex_);
				types_[t_name].state = type_cache_state::unresolved;
				event_logger::get_instance().log(
				    std::format("type_definition_cache: type='{}', loc='{}' outside workspace root '{}' -> unresolved",
						t_name, abs_path, norm_root));
				return;
			}

			std::string rel_path = fs_utils::make_relative_to_project(abs_path);
			int def_start = loc.range.start_y + 1;
			int def_end = loc.range.end_y + 1;

			std::string kind = !loc.kind.empty() ? loc.kind : "struct";
			std::string underlying_type = loc.underlying_type;

			if (kind == "typedef") {
				def_end = def_start;
				// If underlying_type is empty (e.g. indexer provided typedef kind but not the alias),
				// read the source line at def_start to extract the alias (e.g. typedef s64 ktime_t -> s64).
				if (underlying_type.empty()) {
					std::ifstream file(abs_path);
					if (file.is_open()) {
						std::string line_content;
						int current_line = 1;
						while (std::getline(file, line_content)) {
							if (current_line >= def_start && current_line <= def_start + 3) {
								extract_typedef_or_alias(line_content, t_name, kind, underlying_type);
								if (!underlying_type.empty()) {
									break;
								}
							}
							if (current_line > def_start + 3) {
								break;
							}
							++current_line;
						}
					}
				}
			} else {
				expand_range_to_symbol_bounds(abs_path, t_name, def_start, def_end, kind);
				// Check if the symbol at def_start is actually a typedef or type alias
				if (kind != "typedef" && underlying_type.empty() && fs_utils::is_regular_file(abs_path)) {
					std::ifstream file(abs_path);
					if (file.is_open()) {
						std::string line_content;
						int current_line = 1;
						while (std::getline(file, line_content)) {
							if (current_line == def_start) {
								extract_typedef_or_alias(line_content, t_name, kind, underlying_type);
								if (kind == "typedef") {
									def_end = def_start;
								}
								break;
							}
							if (current_line > def_start) {
								break;
							}
							++current_line;
						}
					}
				}
			}

			std::lock_guard<std::mutex> lock(mutex_);
			auto &entry = types_[t_name];
			entry.type_name = t_name;
			entry.state = type_cache_state::resolved;
			entry.safe_file_path = rel_path;
			entry.kind = kind;
			entry.underlying_type = underlying_type;
			entry.start_line = def_start;
			entry.end_line = def_end;
			event_logger::get_instance().log(std::format("type_definition_cache: type='{}' resolved to kind='{}', '{}:{}-{}'",
								     t_name, kind, rel_path, def_start, def_end));
		} catch (...) {
			std::lock_guard<std::mutex> lock(mutex_);
			auto it = types_.find(t_name);
			if (it != types_.end()) {
				it->second.state = type_cache_state::failed;
			}
			event_logger::get_instance().log(
			    std::format("type_definition_cache: type='{}' background worker caught exception -> failed", t_name));
		}
	}).detach();
}

void type_definition_cache::invalidate_file(const std::string &safe_path)
{
	if (safe_path.empty()) {
		return;
	}

	std::lock_guard<std::mutex> lock(mutex_);
	for (auto it = types_.begin(); it != types_.end();) {
		if (it->second.safe_file_path == safe_path) {
			it = types_.erase(it);
		} else {
			++it;
		}
	}
}

void type_definition_cache::clear()
{
	std::lock_guard<std::mutex> lock(mutex_);
	types_.clear();
}

int type_definition_cache::find_closing_brace_line(const std::string &file_path, int start_line, int max_scan_lines)
{
	if (!fs_utils::is_regular_file(file_path) || start_line <= 0) {
		return start_line;
	}

	std::ifstream file(file_path);
	if (!file.is_open()) {
		return start_line;
	}

	std::string line_content;
	int current_line = 1;
	int depth = 0;
	bool started = false;
	int scanned_end = start_line;

	while (std::getline(file, line_content)) {
		if (current_line >= start_line) {
			if (!started) {
				// If a semicolon occurs before any opening brace '{', this is a typedef,
				// type alias, or forward declaration that has no brace body.
				size_t open_brace = line_content.find('{');
				size_t semi = line_content.find(';');
				if (semi != std::string::npos && (open_brace == std::string::npos || semi < open_brace)) {
					return start_line;
				}
			}
			for (char c : line_content) {
				if (c == '{') {
					depth++;
					started = true;
				} else if (c == '}') {
					depth--;
				}
			}
			if (started && depth <= 0) {
				scanned_end = current_line;
				break;
			}
		}
		if (current_line > start_line + max_scan_lines) {
			break;
		}
		++current_line;
	}

	if (started && scanned_end > start_line) {
		return scanned_end;
	}
	return start_line;
}

std::string type_definition_cache::format_type_definition_table(const std::vector<type_definition_entry> &types)
{
	if (types.empty()) {
		return "";
	}

	std::stringstream ss;
	ss << "\n### Type Definitions:\n\n";
	ss << "| Type | Kind | path | start_line | end_line |\n";
	ss << "| :--- | :--- | :--- | :---: | :---: |\n";
	for (const auto &t : types) {
		std::string kind_display = t.kind;
		if (!t.underlying_type.empty()) {
			kind_display = std::format("{} ({})", t.kind, t.underlying_type);
		}
		ss << std::format("| `{}` | {} | `{}` | {} | {} |\n", t.type_name, kind_display, t.safe_file_path, t.start_line,
				  t.end_line);
	}
	return ss.str();
}

} // namespace tools
