#include "semcode_backend.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include "codemap_utils.h"
#include "event_logger.h"
#include "fs_utils.h"
#include "project_manager.h"
#include "utf8.h"

namespace fs = std::filesystem;

semcode_backend::semcode_backend(std::string project_root) : project_root_(std::move(project_root))
{
	build_state_->backend = this;
	if (project_root_.empty()) {
		project_root_ = project_manager::get_instance().get_project_root();
	}
	semcode_lsp_path_ = find_semcode_lsp();
	semcode_cli_path_ = find_semcode_cli();

	init_indexer();

	event_logger::get_instance().log("semcode_backend initialized for root '{}' (semcode-lsp: '{}', semcode-cli: '{}', indexer: {})",
					 project_root_, semcode_lsp_path_, semcode_cli_path_,
					 (indexer_ && indexer_->is_open()) ? "active" : "pending/disabled");
}

semcode_backend::~semcode_backend()
{
	if (build_state_) {
		std::lock_guard<std::mutex> lock(build_state_->mtx);
		build_state_->backend = nullptr;
	}
	stop();
}

void semcode_backend::init_indexer()
{
	if (project_root_.empty()) {
		return;
	}

	std::error_code ec;

	// 1. Check explicit environment override
	const char *env_db = std::getenv("SEMCODE_INDEX_DB");
	if (env_db && *env_db && fs_utils::is_regular_file(env_db)) {
		std::lock_guard<std::mutex> lock(indexer_mutex_);
		indexer_ = std::make_unique<semcode_indexer>(env_db);
		if (indexer_->open()) {
			event_logger::get_instance().log("semcode_backend: loaded index from SEMCODE_INDEX_DB override '{}'", env_db);
			return;
		}
		indexer_.reset();
	}

	fs::path semcode_dir = fs::path(project_root_) / ".semcode.db";

	// 2. Resolve git commit HEAD if in a git repository
	std::string git_cmd = fs_utils::format_command("git -C {} rev-parse HEAD 2>/dev/null", project_root_);
	std::string git_head = fs_utils::execute_command_sync(git_cmd, 5);
	size_t nl = git_head.find_first_of("\r\n");
	if (nl != std::string::npos) {
		git_head = git_head.substr(0, nl);
	}

	// 3. Look for an existing database matching git HEAD or the newest database in .semcode.db/
	fs::path chosen_db;
	bool is_exact_head = false;
	if (!git_head.empty()) {
		fs::path head_db = semcode_dir / std::format("semcode_{}.db", git_head);
		if (fs_utils::is_regular_file(head_db.string())) {
			chosen_db = head_db;
			is_exact_head = true;
		}
	}

	if (chosen_db.empty() && fs::is_directory(semcode_dir, ec)) {
		fs::file_time_type latest_time;
		for (const auto &entry : fs::directory_iterator(semcode_dir, ec)) {
			if (entry.is_regular_file(ec) && entry.path().extension() == ".db") {
				auto mtime = entry.last_write_time(ec);
				if (chosen_db.empty() || mtime > latest_time) {
					latest_time = mtime;
					chosen_db = entry.path();
				}
			}
		}
	}

	if (!chosen_db.empty()) {
		std::lock_guard<std::mutex> lock(indexer_mutex_);
		indexer_ = std::make_unique<semcode_indexer>(chosen_db.string());
		if (indexer_->open()) {
			event_logger::get_instance().log("semcode_backend: opened existing index database '{}' (exact_head: {})",
							 chosen_db.string(), is_exact_head);
			if (is_exact_head) {
				return;
			}
		} else {
			indexer_.reset();
		}
	}

	// 4. If current git HEAD is not yet indexed (or no database exists at all),
	// and semcode CLI is available, build the index for the new git HEAD in the background
	if (!is_exact_head && !semcode_cli_path_.empty() && fs::exists(semcode_dir, ec)) {
		bool expected = false;
		if (!is_building_index_.compare_exchange_strong(expected, true)) {
			return;
		}

		fs::path target_db = semcode_dir / (!git_head.empty() ? std::format("semcode_{}.db", git_head) : "semcode_index.db");
		std::string proj = project_root_;
		std::string bin = semcode_cli_path_;
		auto state = build_state_;

		std::thread([state, proj, bin, target_db]() {
			fs_utils::set_current_thread_name("semcode_bld");
			event_logger::get_instance().log("semcode_backend: triggering background index build for '{}' -> '{}'", proj,
							 target_db.string());
			auto idx = std::make_unique<semcode_indexer>(target_db.string());
			if (idx->build_from_project(proj, bin, 2)) {
				if (idx->open()) {
					std::lock_guard<std::mutex> lock(state->mtx);
					if (state->backend) {
						std::lock_guard<std::mutex> idx_lock(state->backend->indexer_mutex_);
						state->backend->indexer_ = std::move(idx);
						state->backend->is_building_index_.store(false, std::memory_order_relaxed);
						event_logger::get_instance().log(
						    "semcode_backend: background index build completed and loaded");
						return;
					}
				}
			} else {
				event_logger::get_instance().log("semcode_backend: background index build failed for '{}'", proj);
			}
			std::lock_guard<std::mutex> lock(state->mtx);
			if (state->backend) {
				state->backend->is_building_index_.store(false, std::memory_order_relaxed);
			}
		}).detach();
	}
}

void semcode_backend::refresh_indexer()
{
	init_indexer();
}

semcode_indexer *semcode_backend::get_indexer() const noexcept
{
	std::lock_guard<std::mutex> lock(indexer_mutex_);
	return indexer_.get();
}

void semcode_backend::set_indexer(std::unique_ptr<semcode_indexer> indexer)
{
	std::lock_guard<std::mutex> lock(indexer_mutex_);
	indexer_ = std::move(indexer);
}

void semcode_backend::start(event_queue &queue)
{
	standard_lsp_backend::start(queue);
	std::lock_guard<std::mutex> lock(hover_mutex_);
	hover_stopping_.store(false, std::memory_order_release);
	if (!hover_thread_.joinable()) {
		hover_thread_ = std::thread([this]() { hover_worker_loop(); });
	}
}

void semcode_backend::stop()
{
	hover_stopping_.store(true, std::memory_order_release);
	hover_cv_.notify_all();
	if (hover_thread_.joinable()) {
		hover_thread_.join();
	}
	standard_lsp_backend::stop();
}

bool semcode_backend::is_available(const std::string &project_root)
{
	if (project_root.empty()) {
		return false;
	}
	std::error_code ec;
	fs::path db_path = fs::path(project_root) / ".semcode.db";
	if (!fs::exists(db_path, ec)) {
		return false;
	}
	std::string lsp_bin = find_semcode_lsp();
	return !lsp_bin.empty();
}

std::string semcode_backend::find_semcode_lsp()
{
	const char *env_path = std::getenv("SEMCODE_LSP_BIN");
	if (env_path && *env_path) {
		return std::string(env_path);
	}
	return fs_utils::find_executable("semcode-lsp");
}

std::string semcode_backend::find_semcode_cli()
{
	const char *env_path = std::getenv("SEMCODE_BIN");
	if (env_path && *env_path) {
		return std::string(env_path);
	}
	return fs_utils::find_executable("semcode");
}

void semcode_backend::open_document(const std::string &filepath, const std::string &text)
{
	std::string ext = fs::path(filepath).extension().string();
	for (auto &c : ext) {
		c = std::tolower(c);
	}
	if (ext == ".py") {
		standard_lsp_backend::open_document(filepath, text);
		return;
	}

	std::lock_guard<std::mutex> lock(doc_mutex_);
	doc_versions_[filepath] = 1;
}

void semcode_backend::update_document(const std::string &filepath, const std::string &text)
{
	invalidate_hover_cache(filepath);

	std::string ext = fs::path(filepath).extension().string();
	for (auto &c : ext) {
		c = std::tolower(c);
	}
	if (ext == ".py") {
		standard_lsp_backend::update_document(filepath, text);
		return;
	}

	std::lock_guard<std::mutex> lock(doc_mutex_);
	doc_versions_[filepath]++;
}

void semcode_backend::request_hover(const std::string &filepath, int line, int character)
{
	// 1. Cache hit check: immediate non-blocking return with cached payload
	std::string key = std::format("{}:{}:{}", filepath, line, character);
	auto cached = get_cached_hover(key);
	if (cached.has_value()) {
		auto *q = global_queue_.load();
		if (q && !cached->empty()) {
			editor_event ev;
			ev.type = event_type::lsp_hover_result;
			ev.payload = *cached;
			q->push(ev);
		}
		return;
	}

	// 2. Dispatch asynchronous background request
	uint64_t req_id = ++hover_counter_;
	{
		std::lock_guard<std::mutex> lock(hover_mutex_);
		if (!hover_thread_.joinable() && !hover_stopping_.load(std::memory_order_relaxed)) {
			hover_thread_ = std::thread([this]() { hover_worker_loop(); });
		}
		pending_hover_request_ = hover_request{filepath, line, character, req_id};
	}
	hover_cv_.notify_one();
}

void semcode_backend::hover_worker_loop()
{
	fs_utils::set_current_thread_name("semcode_hover");
	while (!hover_stopping_.load(std::memory_order_relaxed)) {
		hover_request req;
		{
			std::unique_lock<std::mutex> lock(hover_mutex_);
			hover_cv_.wait(
			    lock, [this] { return pending_hover_request_.has_value() || hover_stopping_.load(std::memory_order_relaxed); });
			if (hover_stopping_.load(std::memory_order_relaxed)) {
				break;
			}
			req = std::move(*pending_hover_request_);
			pending_hover_request_.reset();
		}

		if (hover_stopping_.load(std::memory_order_relaxed) || project_manager::get_instance().is_exiting()) {
			break;
		}

		if (req.request_id != hover_counter_.load(std::memory_order_relaxed)) {
			continue;
		}

		std::string key = std::format("{}:{}:{}", req.filepath, req.line, req.character);
		auto cached = get_cached_hover(key);
		if (cached.has_value()) {
			auto *q = global_queue_.load();
			if (q && !cached->empty()) {
				editor_event ev;
				ev.type = event_type::lsp_hover_result;
				ev.payload = *cached;
				q->push(ev);
			}
			continue;
		}

		std::string identifier = extract_identifier_at(req.filepath, req.line, req.character);
		if (identifier.empty()) {
			continue;
		}

		std::string hover_md;
		{
			std::lock_guard<std::mutex> lock(indexer_mutex_);
			if (indexer_ && indexer_->is_open()) {
				// 1. Check type definitions
				auto types = indexer_->lookup_type(identifier);
				if (!types.empty()) {
					const auto &t = types[0];
					hover_md =
					    std::format("### {} `{}`\n\n**Defined in**: `{}:{}-{}`\n", t.kind.empty() ? "Type" : t.kind,
							t.name, t.file_path, t.line_start, t.line_end);
					if (!t.underlying_type.empty()) {
						hover_md += std::format("\n**Underlying type**: `{}`\n", t.underlying_type);
					}
				} else {
					// 2. Check function definitions
					auto funcs = indexer_->lookup_function(identifier);
					if (!funcs.empty()) {
						const auto &fn = funcs[0];
						hover_md = std::format("### Function `{}`\n\n**Defined in**: `{}:{}-{}`\n", fn.name,
								       fn.file_path, fn.line_start, fn.line_end);
						if (!fn.calls.empty()) {
							hover_md += "\n**Outgoing calls**:\n";
							for (const auto &c : fn.calls) {
								hover_md += std::format("- `{}`\n", c);
							}
						}
						if (!fn.types.empty()) {
							hover_md += "\n**Types referenced**:\n";
							for (const auto &ty : fn.types) {
								hover_md += std::format("- `{}`\n", ty);
							}
						}
					}
				}
			}
		}

		if (!hover_md.empty()) {
			set_cached_hover(key, hover_md);
			if (!hover_stopping_.load(std::memory_order_relaxed) &&
			    req.request_id == hover_counter_.load(std::memory_order_relaxed)) {
				auto *q = global_queue_.load();
				if (q) {
					editor_event ev;
					ev.type = event_type::lsp_hover_result;
					ev.payload = hover_md;
					q->push(ev);
				}
			}
		}
	}
}

void semcode_backend::request_document_highlight(const std::string & /*filepath*/, int /*line*/, int /*character*/)
{
}

void semcode_backend::request_selection_range(const std::string & /*filepath*/, int /*line*/, int /*character*/)
{
}

bool semcode_backend::is_supported_file(const std::string &filepath) const
{
	std::string ext = fs::path(filepath).extension().string();
	for (auto &c : ext) {
		c = std::tolower(c);
	}
	return (ext == ".cpp" || ext == ".c" || ext == ".h" || ext == ".hpp" || ext == ".cc" || ext == ".cxx" || ext == ".py" ||
		ext == ".rs" || ext == ".zig");
}

std::vector<text_range> semcode_backend::query_selection_ranges(const std::string & /*filepath*/, int /*line*/, int /*character*/)
{
	return {};
}

std::vector<lsp_backend::location_info> semcode_backend::query_definition(const std::string &filepath, int line, int character)
{
	std::string identifier = extract_identifier_at(filepath, line, character);
	if (identifier.empty()) {
		return {};
	}

	std::string norm_caller = fs_utils::make_relative_to_project(filepath, project_root_);

	{
		std::lock_guard<std::mutex> lock(indexer_mutex_);
		if (indexer_ && indexer_->is_open()) {
			// 1. Check function definitions
			auto funcs = indexer_->lookup_function(identifier);
			std::vector<semcode_function_entry> valid_funcs;
			for (auto &f : funcs) {
				std::string rel_path = fs_utils::make_relative_to_project(f.file_path, project_root_);
				if (rel_path.starts_with("arch/") && !norm_caller.starts_with("arch/")) {
					continue;
				}
				if (rel_path.starts_with("arch/") && norm_caller.starts_with("arch/")) {
					size_t caller_slash = norm_caller.find('/', 5);
					size_t callee_slash = rel_path.find('/', 5);
					if (caller_slash != std::string::npos && callee_slash != std::string::npos) {
						if (norm_caller.substr(0, caller_slash) != rel_path.substr(0, callee_slash)) {
							continue;
						}
					}
				}
				valid_funcs.push_back(std::move(f));
			}

			// Deduplicate identical definitions pointing to the same file and line
			std::vector<semcode_function_entry> unique_funcs;
			for (auto &f : valid_funcs) {
				bool exists = false;
				for (const auto &u : unique_funcs) {
					if (u.file_path == f.file_path && u.line_start == f.line_start) {
						exists = true;
						break;
					}
				}
				if (!exists) {
					unique_funcs.push_back(std::move(f));
				}
			}

			if (unique_funcs.size() == 1) {
				const auto &f = unique_funcs[0];
				fs::path full_path = fs::path(f.file_path);
				if (!full_path.is_absolute() && !project_root_.empty()) {
					full_path = fs::path(project_root_) / full_path;
				}
				location_info loc;
				loc.path = full_path.string();
				int zero_start = std::max(0, f.line_start - 1);
				int zero_end = std::max(0, f.line_end - 1);
				loc.range = text_range{zero_start, 0, zero_end, 0};
				loc.kind = "function";
				return {loc};
			} else if (unique_funcs.size() > 1) {
				// Ambiguity detected across multiple functions
				event_logger::get_instance().log(
				    std::format("semcode_backend::query_definition: ambiguous functions for '{}'", identifier));
				return {};
			}

			// 2. Check type definitions
			auto types = indexer_->lookup_type(identifier);
			std::vector<semcode_type_entry> valid_types;
			for (auto &t : types) {
				std::string rel_path = fs_utils::make_relative_to_project(t.file_path, project_root_);
				if (rel_path.starts_with("arch/") && !norm_caller.starts_with("arch/")) {
					continue;
				}
				valid_types.push_back(std::move(t));
			}

			std::vector<semcode_type_entry> unique_types;
			for (auto &t : valid_types) {
				bool exists = false;
				for (auto &u : unique_types) {
					if (u.file_path == t.file_path && u.line_start == t.line_start) {
						exists = true;
						if (u.kind.empty() && !t.kind.empty()) {
							u.kind = t.kind;
						}
						if (u.underlying_type.empty() && !t.underlying_type.empty()) {
							u.underlying_type = t.underlying_type;
						}
						break;
					}
				}
				if (!exists) {
					unique_types.push_back(std::move(t));
				}
			}

			if (unique_types.size() == 1) {
				const auto &t = unique_types[0];
				fs::path full_path = fs::path(t.file_path);
				if (!full_path.is_absolute() && !project_root_.empty()) {
					full_path = fs::path(project_root_) / full_path;
				}
				location_info loc;
				loc.path = full_path.string();
				int zero_start = std::max(0, t.line_start - 1);
				int zero_end = std::max(0, t.line_end - 1);
				loc.range = text_range{zero_start, 0, zero_end, 0};
				loc.kind = t.kind.empty() ? "type" : t.kind;
				loc.underlying_type = t.underlying_type;
				return {loc};
			} else if (unique_types.size() > 1) {
				event_logger::get_instance().log(
				    std::format("semcode_backend::query_definition: ambiguous types for '{}'", identifier));
				return {};
			}
		}
	}

	// 3. Fallback: JSON-RPC query via semcode-lsp
	auto results = standard_lsp_backend::query_definition(filepath, line, character);
	if (results.size() > 1) {
		return {};
	}
	return results;
}

std::vector<lsp_backend::location_info> semcode_backend::query_type_definition(const std::string &filepath, int line, int character)
{
	std::string identifier = extract_identifier_at(filepath, line, character);
	if (identifier.empty()) {
		return {};
	}

	std::string norm_caller = fs_utils::make_relative_to_project(filepath, project_root_);

	{
		std::lock_guard<std::mutex> lock(indexer_mutex_);
		if (indexer_ && indexer_->is_open()) {
			auto types = indexer_->lookup_type(identifier);
			std::vector<semcode_type_entry> valid_types;
			for (auto &t : types) {
				std::string rel_path = fs_utils::make_relative_to_project(t.file_path, project_root_);
				if (rel_path.starts_with("arch/") && !norm_caller.starts_with("arch/")) {
					continue;
				}
				valid_types.push_back(std::move(t));
			}

			std::vector<semcode_type_entry> unique_types;
			for (auto &t : valid_types) {
				bool exists = false;
				for (auto &u : unique_types) {
					if (u.file_path == t.file_path && u.line_start == t.line_start) {
						exists = true;
						if (u.kind.empty() && !t.kind.empty()) {
							u.kind = t.kind;
						}
						if (u.underlying_type.empty() && !t.underlying_type.empty()) {
							u.underlying_type = t.underlying_type;
						}
						break;
					}
				}
				if (!exists) {
					unique_types.push_back(std::move(t));
				}
			}

			if (unique_types.size() == 1) {
				const auto &t = unique_types[0];
				fs::path full_path = fs::path(t.file_path);
				if (!full_path.is_absolute() && !project_root_.empty()) {
					full_path = fs::path(project_root_) / full_path;
				}
				location_info loc;
				loc.path = full_path.string();
				int zero_start = std::max(0, t.line_start - 1);
				int zero_end = std::max(0, t.line_end - 1);
				loc.range = text_range{zero_start, 0, zero_end, 0};
				loc.kind = t.kind.empty() ? "type" : t.kind;
				loc.underlying_type = t.underlying_type;
				return {loc};
			} else if (unique_types.size() > 1) {
				return {};
			}
		}
	}

	auto results = standard_lsp_backend::query_type_definition(filepath, line, character);
	if (results.size() > 1) {
		return {};
	}
	return results;
}

std::vector<lsp_backend::location_info> semcode_backend::query_references(const std::string &filepath, int line, int character)
{
	// 1. First attempt JSON-RPC query via semcode-lsp
	std::vector<location_info> results = standard_lsp_backend::query_references(filepath, line, character);
	if (!results.empty()) {
		return results;
	}

	// 2. Query callers directly from SQLite indexer
	std::string identifier = extract_identifier_at(filepath, line, character);
	if (identifier.empty()) {
		return {};
	}

	std::lock_guard<std::mutex> lock(indexer_mutex_);
	if (indexer_ && indexer_->is_open()) {
		auto callers = indexer_->lookup_callers(identifier);
		for (const auto &c : callers) {
			fs::path full_path = fs::path(c.file_path);
			if (!full_path.is_absolute() && !project_root_.empty()) {
				full_path = fs::path(project_root_) / full_path;
			}
			location_info loc;
			loc.path = full_path.string();
			int zero_line = std::max(0, c.line_start - 1);
			loc.range = text_range{zero_line, 0, zero_line, 0};
			results.push_back(std::move(loc));
		}
	}

	return results;
}

std::vector<lsp_backend::symbol_info> semcode_backend::query_workspace_symbols(const std::string &query)
{
	std::lock_guard<std::mutex> lock(indexer_mutex_);
	if (!indexer_ || !indexer_->is_open()) {
		return standard_lsp_backend::query_workspace_symbols(query);
	}

	std::vector<symbol_info> symbols;

	auto funcs = indexer_->lookup_functions_by_prefix(query, 50);
	for (const auto &fn : funcs) {
		fs::path full_path = fs::path(fn.file_path);
		if (!full_path.is_absolute() && !project_root_.empty()) {
			full_path = fs::path(project_root_) / full_path;
		}
		symbol_info s;
		s.name = fn.name;
		s.kind = 12; // Function
		int zero_start = std::max(0, fn.line_start - 1);
		int zero_end = std::max(0, fn.line_end - 1);
		s.location.path = full_path.string();
		s.location.range = text_range{zero_start, 0, zero_end, 0};
		symbols.push_back(std::move(s));
	}

	auto types = indexer_->lookup_types_by_prefix(query, 50);
	for (const auto &ty : types) {
		fs::path full_path = fs::path(ty.file_path);
		if (!full_path.is_absolute() && !project_root_.empty()) {
			full_path = fs::path(project_root_) / full_path;
		}
		symbol_info s;
		s.name = ty.name;
		s.kind = 5; // Class / Struct
		int zero_start = std::max(0, ty.line_start - 1);
		int zero_end = std::max(0, ty.line_end - 1);
		s.location.path = full_path.string();
		s.location.range = text_range{zero_start, 0, zero_end, 0};
		symbols.push_back(std::move(s));
	}

	return symbols;
}

std::vector<lsp_backend::symbol_node> semcode_backend::query_document_symbols(const std::string &filepath)
{
	std::vector<tools::codemap_symbol_info> syms;
	tools::fallback_find_symbols(filepath, 1, syms);
	std::vector<symbol_node> nodes;
	nodes.reserve(syms.size());
	for (const auto &s : syms) {
		symbol_node node;
		node.name = s.name;
		node.kind = 12; // Function
		node.range.start_y = s.start_line - 1;
		node.range.start_x = 0;
		node.range.end_y = s.end_line - 1;
		node.range.end_x = 0;
		node.selection_range = node.range;
		nodes.push_back(std::move(node));
	}
	return nodes;
}

std::vector<lsp_backend::call_hierarchy_item>
semcode_backend::query_call_hierarchy_outgoing(const std::string &filepath, int line, int character,
					       std::chrono::steady_clock::time_point /*deadline*/)
{
	std::lock_guard<std::mutex> lock(indexer_mutex_);
	if (!indexer_ || !indexer_->is_open()) {
		return {};
	}

	std::string norm_caller = fs_utils::make_relative_to_project(filepath, project_root_);
	int one_based_line = line + 1;

	// 1. Identify outgoing calls from the enclosing function
	std::vector<std::string> raw_calls;
	auto enclosing = indexer_->lookup_functions_in_file(norm_caller, one_based_line, one_based_line);
	if (!enclosing.empty()) {
		raw_calls = enclosing[0].calls;
	} else {
		std::string id = extract_identifier_at(filepath, line, character);
		if (!id.empty()) {
			auto fn_by_name = indexer_->lookup_function(id);
			if (!fn_by_name.empty()) {
				raw_calls = fn_by_name[0].calls;
			}
		}
	}

	if (raw_calls.empty()) {
		return {};
	}

	std::vector<call_hierarchy_item> items;
	items.reserve(raw_calls.size());

	// 2. Resolve each callee to its unique definition
	for (const auto &callee_name : raw_calls) {
		auto defs = indexer_->lookup_function(callee_name);
		std::vector<semcode_function_entry> matching_defs;
		for (auto &d : defs) {
			std::string d_path = fs_utils::make_relative_to_project(d.file_path, project_root_);
			if (d_path.starts_with("arch/") && !norm_caller.starts_with("arch/")) {
				continue;
			}
			if (d_path.starts_with("arch/") && norm_caller.starts_with("arch/")) {
				size_t caller_slash = norm_caller.find('/', 5);
				size_t cand_slash = d_path.find('/', 5);
				if (caller_slash != std::string::npos && cand_slash != std::string::npos) {
					if (norm_caller.substr(0, caller_slash) != d_path.substr(0, cand_slash)) {
						continue;
					}
				}
			}
			matching_defs.push_back(std::move(d));
		}

		if (matching_defs.size() == 1) {
			const auto &def = matching_defs[0];
			fs::path full_path = fs::path(def.file_path);
			if (!full_path.is_absolute() && !project_root_.empty()) {
				full_path = fs::path(project_root_) / full_path;
			}
			call_hierarchy_item item;
			item.name = def.name;
			item.uri = "file://" + full_path.string();
			int zero_start = std::max(0, def.line_start - 1);
			int zero_end = std::max(0, def.line_end - 1);
			item.range = text_range{zero_start, 0, zero_end, 0};
			item.selection_range = item.range;
			items.push_back(std::move(item));
		}
	}

	return items;
}

std::vector<lsp_backend::outgoing_call_item>
semcode_backend::query_call_hierarchy_outgoing_batch(const std::string &filepath, const std::vector<std::pair<int, int>> &positions,
						     std::chrono::steady_clock::time_point deadline)
{
	std::vector<outgoing_call_item> batch_results;
	for (const auto &[line, character] : positions) {
		auto calls = query_call_hierarchy_outgoing(filepath, line, character, deadline);
		for (auto &call : calls) {
			outgoing_call_item out_item;
			out_item.call_line = line;
			out_item.item = std::move(call);
			batch_results.push_back(std::move(out_item));
		}
	}

	std::stable_sort(batch_results.begin(), batch_results.end(), [](const outgoing_call_item &a, const outgoing_call_item &b) {
		if (a.call_line != b.call_line) {
			return a.call_line < b.call_line;
		}
		return a.item.name < b.item.name;
	});

	return batch_results;
}

std::vector<lsp_backend::type_hierarchy_item> semcode_backend::query_type_hierarchy_supertypes(const std::string &filepath, int line,
											       int character)
{
	std::string identifier = extract_identifier_at(filepath, line, character);
	if (identifier.empty()) {
		return {};
	}

	std::vector<type_hierarchy_item> items;
	{
		std::lock_guard<std::mutex> lock(indexer_mutex_);
		if (indexer_ && indexer_->is_open()) {
			auto types = indexer_->lookup_type(identifier);
			for (const auto &t : types) {
				fs::path full_path = fs::path(t.file_path);
				if (!full_path.is_absolute() && !project_root_.empty()) {
					full_path = fs::path(project_root_) / full_path;
				}
				type_hierarchy_item item;
				item.name = identifier;
				item.kind = 5; // Class / Struct
				item.detail = t.kind.empty() ? "type" : t.kind;
				item.uri = "file://" + full_path.string();
				int zero_start = std::max(0, t.line_start - 1);
				int zero_end = std::max(0, t.line_end - 1);
				item.range = text_range{zero_start, 0, zero_end, 0};
				item.selection_range = item.range;
				items.push_back(std::move(item));
			}
		}
	}
	return items;
}

std::shared_ptr<standard_lsp_backend::server_instance> semcode_backend::get_server_for_file(const std::string &filepath)
{
	std::string ext = fs::path(filepath).extension().string();
	for (auto &c : ext) {
		c = std::tolower(c);
	}

	if (ext == ".py") {
		return standard_lsp_backend::get_server_for_file(filepath);
	}

	bool is_c_cpp = (ext == ".cpp" || ext == ".c" || ext == ".h" || ext == ".hpp" || ext == ".cc" || ext == ".cxx");
	if (!is_c_cpp) {
		return nullptr;
	}

	std::lock_guard<std::mutex> lock(servers_mutex_);
	for (auto &server : servers_) {
		if (server->language_id == "cpp" && server->is_running) {
			return server;
		}
	}

	if (!semcode_lsp_path_.empty()) {
		start_server(semcode_lsp_path_, {}, "cpp");
	}

	for (auto &server : servers_) {
		if (server->language_id == "cpp" && server->is_running) {
			return server;
		}
	}

	return nullptr;
}

std::string semcode_backend::extract_identifier_at(const std::string &filepath, int line, int character)
{
	if (line < 0 || character < 0) {
		return "";
	}
	std::ifstream file(filepath);
	if (!file.is_open()) {
		return "";
	}
	std::string current_line;
	int current_index = 0;
	while (std::getline(file, current_line)) {
		if (current_index == line) {
			break;
		}
		++current_index;
	}
	if (current_index != line || current_line.empty()) {
		return "";
	}
	if (character >= static_cast<int>(current_line.size())) {
		character = static_cast<int>(current_line.size()) - 1;
	}
	if (character < 0) {
		return "";
	}

	size_t start = static_cast<size_t>(character);
	while (start > 0 && (std::isalnum(static_cast<unsigned char>(current_line[start - 1])) || current_line[start - 1] == '_')) {
		--start;
	}
	size_t end = static_cast<size_t>(character);
	while (end < current_line.size() && (std::isalnum(static_cast<unsigned char>(current_line[end])) || current_line[end] == '_')) {
		++end;
	}
	if (start >= end) {
		return "";
	}
	std::string token = current_line.substr(start, end - start);

	static const std::unordered_set<std::string_view> specifiers = {
	    "static",	"inline",  "virtual",	 "explicit", "extern",	"constexpr", "consteval", "noexcept", "noinstr", "__always_inline",
	    "__init",	"__exit",  "asmlinkage", "__sched",  "void",	"int",	     "bool",	  "char",     "long",	 "short",
	    "unsigned", "signed",  "size_t",	 "ssize_t",  "uint8_t", "uint16_t",  "uint32_t",  "uint64_t", "int8_t",	 "int16_t",
	    "int32_t",	"int64_t", "u8",	 "u16",	     "u32",	"u64",	     "s8",	  "s16",      "s32",	 "s64",
	    "ktime_t",	"auto",	   "double",	 "float",    "const",	"struct",    "class",	  "enum"};

	if (character == 0 && specifiers.contains(token)) {
		size_t scan = end;
		while (scan < current_line.size()) {
			while (scan < current_line.size() && !std::isalpha(static_cast<unsigned char>(current_line[scan])) &&
			       current_line[scan] != '_') {
				scan++;
			}
			size_t w_start = scan;
			while (scan < current_line.size() &&
			       (std::isalnum(static_cast<unsigned char>(current_line[scan])) || current_line[scan] == '_')) {
				scan++;
			}
			if (scan > w_start) {
				std::string candidate = current_line.substr(w_start, scan - w_start);
				if (!specifiers.contains(candidate)) {
					return candidate;
				}
			}
		}
	}

	return token;
}
