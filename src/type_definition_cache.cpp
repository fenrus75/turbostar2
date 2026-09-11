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
			// Already tracked, pending, or resolved
			return;
		}

		type_definition_entry pending_stub;
		pending_stub.type_name = t_name;
		pending_stub.state = type_cache_state::pending;
		pending_stub.requested_at = std::chrono::steady_clock::now();
		types_[t_name] = pending_stub;
	}

	// Dispatch non-blocking background resolution worker
	std::thread([this, t_name, referencing_file, line, character]() {
		try {
			if (!project_manager::get_instance().lsp_is_supported_file(referencing_file)) {
				std::lock_guard<std::mutex> lock(mutex_);
				types_[t_name].state = type_cache_state::unresolved;
				return;
			}

			auto locs = project_manager::get_instance().lsp_query_definition(referencing_file, line, character);
			if (locs.empty()) {
				std::lock_guard<std::mutex> lock(mutex_);
				types_[t_name].state = type_cache_state::unresolved;
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
				return;
			}

			std::string rel_path = fs_utils::make_relative_to_project(abs_path);
			int def_start = loc.range.start_y + 1;
			int def_end = loc.range.end_y + 1;

			// Obtain document symbols to find the enclosing struct/class scope
			auto doc_symbols = get_document_codemap_symbols(rel_path, 1);
			const codemap_symbol_info *enclosing = find_enclosing_symbol(doc_symbols, def_start);
			std::string kind = "struct";
			if (enclosing && (enclosing->kind_str == "Class" || enclosing->kind_str == "Struct" ||
					  enclosing->kind_str == "Enum" || enclosing->kind_str == "Interface")) {
				def_start = enclosing->start_line;
				def_end = enclosing->end_line;
				kind = (enclosing->kind_str == "Class") ? "class" : ((enclosing->kind_str == "Enum") ? "enum" : "struct");
			}

			std::lock_guard<std::mutex> lock(mutex_);
			auto &entry = types_[t_name];
			entry.type_name = t_name;
			entry.state = type_cache_state::resolved;
			entry.safe_file_path = rel_path;
			entry.kind = kind;
			entry.start_line = def_start;
			entry.end_line = def_end;
		} catch (...) {
			std::lock_guard<std::mutex> lock(mutex_);
			auto it = types_.find(t_name);
			if (it != types_.end()) {
				it->second.state = type_cache_state::failed;
			}
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
	ss << "| Type | Kind | Defined In | Lines |\n";
	ss << "| :--- | :--- | :--- | :---: |\n";
	for (const auto &t : types) {
		std::string lines_str =
		    (t.start_line == t.end_line) ? std::format("{}", t.start_line) : std::format("{}-{}", t.start_line, t.end_line);
		ss << std::format("| `{}` | {} | `{}` | {} |\n", t.type_name, t.kind, t.safe_file_path, lines_str);
	}
	return ss.str();
}

} // namespace tools
