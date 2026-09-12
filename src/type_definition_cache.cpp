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
						   int start_line, int end_line)
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
	entry.start_line = start_line;
	entry.end_line = end_line;
	entry.requested_at = std::chrono::steady_clock::now();

	types_[std::string(type_name)] = std::move(entry);
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
			event_logger::get_instance().log(std::format(
				"type_definition_cache::request_async: type='{}' already tracked (state={})",
				t_name, static_cast<int>(it->second.state)));
			return;
		}

		type_definition_entry pending_stub;
		pending_stub.type_name = t_name;
		pending_stub.state = type_cache_state::pending;
		pending_stub.requested_at = std::chrono::steady_clock::now();
		types_[t_name] = pending_stub;
		event_logger::get_instance().log(std::format(
			"type_definition_cache::request_async: dispatched background worker for type='{}' (ref='{}:{}:{}')",
			t_name, referencing_file, line, character));
	}

	// Dispatch non-blocking background resolution worker
	std::thread([this, t_name, referencing_file, line, character]() {
		fs_utils::set_current_thread_name("type_cache");
		try {
			if (!project_manager::get_instance().lsp_is_supported_file(referencing_file)) {
				std::lock_guard<std::mutex> lock(mutex_);
				types_[t_name].state = type_cache_state::unresolved;
				event_logger::get_instance().log(std::format(
					"type_definition_cache: type='{}', file='{}' not supported by LSP -> unresolved",
					t_name, referencing_file));
				return;
			}

			auto locs = project_manager::get_instance().lsp_query_definition(referencing_file, line, character);
			if (locs.empty()) {
				std::lock_guard<std::mutex> lock(mutex_);
				types_[t_name].state = type_cache_state::unresolved;
				event_logger::get_instance().log(std::format(
					"type_definition_cache: type='{}', no definition locations found -> unresolved",
					t_name));
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
				event_logger::get_instance().log(std::format(
					"type_definition_cache: type='{}', loc='{}' outside workspace root '{}' -> unresolved",
					t_name, abs_path, norm_root));
				return;
			}

			std::string rel_path = fs_utils::make_relative_to_project(abs_path);
			int def_start = loc.range.start_y + 1;
			int def_end = loc.range.end_y + 1;

			// Obtain document symbols to find the enclosing struct/class scope
			auto doc_symbols = get_document_codemap_symbols(rel_path, 1);
			const codemap_symbol_info *target_sym = find_symbol_by_hint(doc_symbols, t_name);
			if (!target_sym) {
				target_sym = find_enclosing_symbol(doc_symbols, def_start);
			}

			std::string kind = "struct";
			if (target_sym && (target_sym->kind_str.find("Class") != std::string::npos ||
					   target_sym->kind_str.find("Struct") != std::string::npos ||
					   target_sym->kind_str == "Enum" || target_sym->kind_str == "Interface")) {
				def_start = target_sym->start_line;
				def_end = target_sym->end_line;
				if (target_sym->kind_str == "Class") {
					kind = "class";
				} else if (target_sym->kind_str == "Enum") {
					kind = "enum";
				} else if (target_sym->kind_str == "Interface") {
					kind = "interface";
				} else {
					kind = "struct";
				}
			}

			// If end line is still equal to or smaller than start line (e.g. LSP returned a 1-line range
			// or symbol AST didn't capture the full block), inspect the target file to determine the true
			// closing brace of the struct/class/enum definition.
			if (def_end <= def_start) {
				std::ifstream file(abs_path);
				if (file.is_open()) {
					std::string line_content;
					int current_line = 1;
					int depth = 0;
					bool started = false;
					int scanned_end = def_start;

					while (std::getline(file, line_content)) {
						if (current_line >= def_start) {
							if (!started) {
								if (line_content.find("enum ") != std::string::npos) {
									kind = "enum";
								} else if (line_content.find("class ") != std::string::npos) {
									kind = "class";
								} else if (line_content.find("struct ") != std::string::npos) {
									kind = "struct";
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
						if (current_line > def_start + 1000) {
							break;
						}
						++current_line;
					}
					if (started && scanned_end > def_start) {
						def_end = scanned_end;
					}
				}
			}

			std::lock_guard<std::mutex> lock(mutex_);
			auto &entry = types_[t_name];
			entry.type_name = t_name;
			entry.state = type_cache_state::resolved;
			entry.safe_file_path = rel_path;
			entry.kind = kind;
			entry.start_line = def_start;
			entry.end_line = def_end;
			event_logger::get_instance().log(std::format(
				"type_definition_cache: type='{}' resolved to kind='{}', '{}:{}-{}'",
				t_name, kind, rel_path, def_start, def_end));
		} catch (...) {
			std::lock_guard<std::mutex> lock(mutex_);
			auto it = types_.find(t_name);
			if (it != types_.end()) {
				it->second.state = type_cache_state::failed;
			}
			event_logger::get_instance().log(std::format(
				"type_definition_cache: type='{}' background worker caught exception -> failed",
				t_name));
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
		ss << std::format("| `{}` | {} | `{}` | {} | {} |\n", t.type_name, t.kind, t.safe_file_path, t.start_line, t.end_line);
	}
	return ss.str();
}

} // namespace tools
