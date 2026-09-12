#include "semcode_backend.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include "event_logger.h"
#include "fs_utils.h"
#include "project_manager.h"
#include "utf8.h"

namespace fs = std::filesystem;

static void parse_file_and_line_locations(std::string_view text, std::string_view root, std::vector<lsp_backend::location_info> &out)
{
	std::istringstream stream{std::string(text)};
	std::string line_str;
	std::string current_file;

	while (std::getline(stream, line_str)) {
		while (!line_str.empty() && (line_str.back() == '\r' || line_str.back() == ' ')) {
			line_str.pop_back();
		}
		size_t first = line_str.find_first_not_of(" \t");
		if (first == std::string::npos) {
			continue;
		}
		std::string_view trimmed = std::string_view(line_str).substr(first);

		if (trimmed.starts_with("File:")) {
			std::string_view f = trimmed.substr(5);
			size_t non_ws = f.find_first_not_of(" \t");
			if (non_ws != std::string_view::npos) {
				current_file = std::string(f.substr(non_ws));
			}
		} else if (trimmed.starts_with("Line:") || trimmed.starts_with("Lines:")) {
			if (!current_file.empty()) {
				size_t colon = trimmed.find(':');
				std::string_view l = trimmed.substr(colon + 1);
				size_t non_ws = l.find_first_not_of(" \t");
				if (non_ws != std::string_view::npos) {
					l = l.substr(non_ws);
					size_t dash = l.find('-');
					if (dash != std::string_view::npos) {
						l = l.substr(0, dash);
					}
					try {
						int line_num = std::stoi(std::string(l));
						fs::path full_path = fs::path(current_file);
						if (!full_path.is_absolute() && !root.empty()) {
							full_path = fs::path(root) / full_path;
						}
						lsp_backend::location_info loc;
						loc.path = full_path.string();
						int zero_line = std::max(0, line_num - 1);
						loc.range = text_range{zero_line, 0, zero_line, 0};
						out.push_back(std::move(loc));
					} catch (...) {
					}
				}
				current_file.clear();
			}
		}
	}
}

// Parses definition locations from semcode CLI 'func' or 'type' query.
// Strictly fails immediately if multiple definitions or locations are detected or if
// semcode's multi-definition banner is present, ensuring zero false dependencies.
// Returns false if ambiguous (multiple definitions found), true if unambiguous (0 or 1 location).
static bool parse_definition_locations_strict_unique(std::string_view text, std::string_view root,
						     std::vector<lsp_backend::location_info> &out)
{
	out.clear();

	// Check for semcode multi-definition banner: e.g. "Found 5 function definitions with name ..."
	if (text.find(" definitions with name ") != std::string_view::npos) {
		return false;
	}

	std::istringstream stream{std::string(text)};
	std::string line_str;
	std::string current_file;

	while (std::getline(stream, line_str)) {
		while (!line_str.empty() && (line_str.back() == '\r' || line_str.back() == ' ')) {
			line_str.pop_back();
		}
		size_t first = line_str.find_first_not_of(" \t");
		if (first == std::string::npos) {
			continue;
		}
		std::string_view trimmed = std::string_view(line_str).substr(first);

		if (trimmed.starts_with("File:")) {
			// If we already resolved a definition location, seeing another File: indicates multiple locations: fail immediately!
			if (!out.empty()) {
				out.clear();
				return false;
			}
			std::string_view f = trimmed.substr(5);
			size_t non_ws = f.find_first_not_of(" \t");
			if (non_ws != std::string_view::npos) {
				current_file = std::string(f.substr(non_ws));
			}
		} else if (trimmed.starts_with("Line:") || trimmed.starts_with("Lines:")) {
			if (!current_file.empty()) {
				// If we already resolved a definition location, another line definition means multiple locations: fail immediately!
				if (!out.empty()) {
					out.clear();
					return false;
				}
				size_t colon = trimmed.find(':');
				std::string_view l = trimmed.substr(colon + 1);
				size_t non_ws = l.find_first_not_of(" \t");
				if (non_ws != std::string_view::npos) {
					l = l.substr(non_ws);
					size_t dash = l.find('-');
					if (dash != std::string_view::npos) {
						l = l.substr(0, dash);
					}
					try {
						int line_num = std::stoi(std::string(l));
						fs::path full_path = fs::path(current_file);
						if (!full_path.is_absolute() && !root.empty()) {
							full_path = fs::path(root) / full_path;
						}
						lsp_backend::location_info loc;
						loc.path = full_path.string();
						int zero_line = std::max(0, line_num - 1);
						loc.range = text_range{zero_line, 0, zero_line, 0};
						out.push_back(std::move(loc));
					} catch (...) {
					}
				}
				current_file.clear();
			}
		}
	}

	if (out.size() > 1) {
		out.clear();
		return false;
	}

	return true;
}

static void parse_callers_locations(std::string_view text, std::string_view root, std::vector<lsp_backend::location_info> &out)
{
	std::istringstream stream{std::string(text)};
	std::string line_str;

	while (std::getline(stream, line_str)) {
		size_t open_paren = line_str.find('(');
		if (open_paren == std::string::npos) {
			continue;
		}
		size_t close_paren = line_str.find(')', open_paren);
		if (close_paren == std::string::npos) {
			continue;
		}
		std::string_view inner = std::string_view(line_str).substr(open_paren + 1, close_paren - open_paren - 1);
		size_t colon = inner.find(':');
		if (colon == std::string_view::npos) {
			continue;
		}
		std::string_view file_part = inner.substr(0, colon);
		std::string_view line_part = inner.substr(colon + 1);
		if (file_part.empty() || line_part.empty()) {
			continue;
		}
		try {
			int line_num = std::stoi(std::string(line_part));
			fs::path full_path = fs::path(file_part);
			if (!full_path.is_absolute() && !root.empty()) {
				full_path = fs::path(root) / full_path;
			}
			lsp_backend::location_info loc;
			loc.path = full_path.string();
			int zero_line = std::max(0, line_num - 1);
			loc.range = text_range{zero_line, 0, zero_line, 0};
			out.push_back(std::move(loc));
		} catch (...) {
		}
	}
}

static void parse_calls_hierarchy(std::string_view text, std::string_view root, std::vector<lsp_backend::call_hierarchy_item> &out)
{
	std::istringstream stream{std::string(text)};
	std::string line_str;
	std::string current_callee;

	while (std::getline(stream, line_str)) {
		while (!line_str.empty() && (line_str.back() == '\r' || line_str.back() == ' ')) {
			line_str.pop_back();
		}
		size_t first = line_str.find_first_not_of(" \t");
		if (first == std::string::npos) {
			continue;
		}
		std::string_view trimmed = std::string_view(line_str).substr(first);

		if (std::isdigit(static_cast<unsigned char>(trimmed.front()))) {
			size_t dot = trimmed.find('.');
			if (dot != std::string_view::npos) {
				std::string_view name_part = trimmed.substr(dot + 1);
				size_t nw = name_part.find_first_not_of(" \t");
				if (nw != std::string_view::npos) {
					current_callee = std::string(name_part.substr(nw));
				}
			}
		} else if (!current_callee.empty()) {
			size_t open_paren = trimmed.find('(');
			size_t close_paren = (open_paren != std::string::npos) ? trimmed.find(')', open_paren) : std::string::npos;
			if (open_paren != std::string::npos && close_paren != std::string::npos) {
				std::string_view detail = trimmed.substr(0, open_paren);
				while (!detail.empty() && (detail.back() == ' ' || detail.back() == '\t')) {
					detail.remove_suffix(1);
				}
				std::string_view inner = trimmed.substr(open_paren + 1, close_paren - open_paren - 1);
				size_t colon = inner.find(':');
				if (colon != std::string_view::npos) {
					std::string_view file_part = inner.substr(0, colon);
					std::string_view line_part = inner.substr(colon + 1);
					try {
						int line_num = std::stoi(std::string(line_part));
						fs::path full_path = fs::path(file_part);
						if (!full_path.is_absolute() && !root.empty()) {
							full_path = fs::path(root) / full_path;
						}
						lsp_backend::call_hierarchy_item item;
						item.name = current_callee;
						item.detail = std::string(detail);
						item.uri = "file://" + full_path.string();
						int zero_line = std::max(0, line_num - 1);
						item.range = text_range{zero_line, 0, zero_line, 0};
						item.selection_range = text_range{zero_line, 0, zero_line, 0};
						out.push_back(std::move(item));
					} catch (...) {
					}
				}
			}
			current_callee.clear();
		}
	}
}

semcode_backend::semcode_backend(std::string project_root)
	: project_root_(std::move(project_root))
{
	if (project_root_.empty()) {
		project_root_ = project_manager::get_instance().get_project_root();
	}
	semcode_lsp_path_ = find_semcode_lsp();
	semcode_cli_path_ = find_semcode_cli();

	event_logger::get_instance().log(
		"semcode_backend initialized for root '{}' (semcode-lsp: '{}', semcode-cli: '{}')",
		project_root_, semcode_lsp_path_, semcode_cli_path_);
}

semcode_backend::~semcode_backend()
{
	stop();
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
	// pylsp still requires document open notifications for Python files
	if (ext == ".py") {
		standard_lsp_backend::open_document(filepath, text);
		return;
	}

	// For semcode-indexed languages, semcode uses the working directory overlay
	// and does not implement textDocument/didOpen. Track versions locally.
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
	if (semcode_cli_path_.empty()) {
		return;
	}

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

	// 2. Cache miss: dispatch asynchronous background request without stalling UI thread
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
			hover_cv_.wait(lock, [this] {
				return pending_hover_request_.has_value() || hover_stopping_.load(std::memory_order_relaxed);
			});
			if (hover_stopping_.load(std::memory_order_relaxed)) {
				break;
			}
			req = std::move(*pending_hover_request_);
			pending_hover_request_.reset();
		}

		if (hover_stopping_.load(std::memory_order_relaxed) || project_manager::get_instance().is_exiting()) {
			break;
		}

		// Skip stale request if user already navigated to a subsequent token
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

		// 1. Try type lookup via semcode CLI (utilizing query cache)
		std::string type_output = run_semcode_query(std::format("type {}", identifier));
		if (!type_output.empty() && (type_output.find("=== Type Information ===") != std::string::npos ||
					     type_output.find("Type Definition:") != std::string::npos ||
					     type_output.find("Fields:") != std::string::npos)) {
			set_cached_hover(key, type_output);
			if (!hover_stopping_.load(std::memory_order_relaxed) &&
			    req.request_id == hover_counter_.load(std::memory_order_relaxed)) {
				auto *q = global_queue_.load();
				if (q) {
					editor_event ev;
					ev.type = event_type::lsp_hover_result;
					ev.payload = type_output;
					q->push(ev);
				}
			}
			continue;
		}

		if (hover_stopping_.load(std::memory_order_relaxed) ||
		    req.request_id != hover_counter_.load(std::memory_order_relaxed)) {
			continue;
		}

		// 2. Fall back to function lookup via semcode CLI (utilizing query cache)
		std::string func_output = run_semcode_query(std::format("func {}", identifier));
		if (!func_output.empty() && (func_output.find("Function Definition:") != std::string::npos ||
					     func_output.find("Return type:") != std::string::npos)) {
			set_cached_hover(key, func_output);
			if (!hover_stopping_.load(std::memory_order_relaxed) &&
			    req.request_id == hover_counter_.load(std::memory_order_relaxed)) {
				auto *q = global_queue_.load();
				if (q) {
					editor_event ev;
					ev.type = event_type::lsp_hover_result;
					ev.payload = func_output;
					q->push(ev);
				}
			}
		}
	}
}

void semcode_backend::request_document_highlight(const std::string &/*filepath*/, int /*line*/, int /*character*/)
{
	// semcode does not track in-memory AST token highlights; gracefully no-op
}

void semcode_backend::request_selection_range(const std::string &/*filepath*/, int /*line*/, int /*character*/)
{
	// semcode does not compute semantic selection ranges; gracefully no-op
}

bool semcode_backend::is_supported_file(const std::string &filepath) const
{
	std::string ext = fs::path(filepath).extension().string();
	for (auto &c : ext) {
		c = std::tolower(c);
	}
	return (ext == ".cpp" || ext == ".c" || ext == ".h" || ext == ".hpp" ||
		ext == ".cc" || ext == ".cxx" || ext == ".py" || ext == ".rs" || ext == ".zig");
}

std::vector<text_range> semcode_backend::query_selection_ranges(const std::string &/*filepath*/, int /*line*/, int /*character*/)
{
	return {};
}

std::vector<lsp_backend::location_info> semcode_backend::query_definition(const std::string &filepath, int line, int character)
{
	std::vector<location_info> results;

	// 1. Primary resolution: query semcode CLI for symbol definition
	// Semcode CLI searches across the entire indexed codebase. If multiple definitions
	// exist, we strictly fail immediately to prevent returning incorrect definitions.
	if (!semcode_cli_path_.empty()) {
		std::string identifier = extract_identifier_at(filepath, line, character);
		if (!identifier.empty()) {
			std::string func_out = run_semcode_query(std::format("func {}", identifier));
			bool func_ok = parse_definition_locations_strict_unique(func_out, project_root_, results);
			if (!func_ok) {
				// Ambiguity detected: multiple function definitions exist, fail immediately
				return {};
			}

			if (!results.empty()) {
				return results;
			}

			// If not a function, check if it is a type definition
			std::string type_out = run_semcode_query(std::format("type {}", identifier));
			bool type_ok = parse_definition_locations_strict_unique(type_out, project_root_, results);
			if (!type_ok) {
				// Ambiguity detected: multiple type definitions exist, fail immediately
				return {};
			}

			if (!results.empty()) {
				return results;
			}
		}
		return {};
	}

	// 2. Fallback if semcode CLI binary is not available: JSON-RPC query via semcode-lsp
	results = standard_lsp_backend::query_definition(filepath, line, character);
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

	// 2. Hybrid fallback: query callers via semcode CLI
	if (!semcode_cli_path_.empty()) {
		std::string identifier = extract_identifier_at(filepath, line, character);
		if (!identifier.empty()) {
			std::string callers_out = run_semcode_query(std::format("callers -v {}", identifier));
			parse_callers_locations(callers_out, project_root_, results);
		}
	}

	return results;
}

std::vector<lsp_backend::symbol_info> semcode_backend::query_workspace_symbols(const std::string &query)
{
	if (semcode_cli_path_.empty() || query.empty()) {
		return {};
	}

	std::vector<symbol_info> symbols;

	// Query functions matching pattern
	std::string func_out = run_semcode_query(std::format("func {}", query));
	std::vector<location_info> func_locs;
	parse_file_and_line_locations(func_out, project_root_, func_locs);
	for (auto &loc : func_locs) {
		symbol_info s;
		s.name = query;
		s.kind = 12; // Function
		s.location = std::move(loc);
		symbols.push_back(std::move(s));
	}

	// Query types matching pattern
	std::string type_out = run_semcode_query(std::format("type {}", query));
	std::vector<location_info> type_locs;
	parse_file_and_line_locations(type_out, project_root_, type_locs);
	for (auto &loc : type_locs) {
		symbol_info s;
		s.name = query;
		s.kind = 5; // Class / Struct
		s.location = std::move(loc);
		symbols.push_back(std::move(s));
	}

	return symbols;
}

std::vector<lsp_backend::symbol_node> semcode_backend::query_document_symbols(const std::string &/*filepath*/)
{
	// semcode does not implement per-file hierarchical symbol trees; return empty
	return {};
}

std::vector<lsp_backend::call_hierarchy_item> semcode_backend::query_call_hierarchy_outgoing(
	const std::string &filepath, int line, int character)
{
	if (semcode_cli_path_.empty()) {
		return {};
	}

	std::string identifier = extract_identifier_at(filepath, line, character);
	if (identifier.empty()) {
		return {};
	}

	std::string calls_out = run_semcode_query(std::format("calls -v {}", identifier));
	std::vector<call_hierarchy_item> items;
	parse_calls_hierarchy(calls_out, project_root_, items);
	return items;
}

std::vector<lsp_backend::outgoing_call_item> semcode_backend::query_call_hierarchy_outgoing_batch(
	const std::string &filepath,
	const std::vector<std::pair<int, int>> &positions,
	std::chrono::steady_clock::time_point deadline)
{
	std::vector<outgoing_call_item> batch_results;
	for (const auto &[line, character] : positions) {
		if (std::chrono::steady_clock::now() > deadline) {
			break;
		}
		auto calls = query_call_hierarchy_outgoing(filepath, line, character);
		for (auto &call : calls) {
			outgoing_call_item out_item;
			out_item.call_line = line;
			out_item.item = std::move(call);
			batch_results.push_back(std::move(out_item));
		}
	}
	return batch_results;
}

std::vector<lsp_backend::type_hierarchy_item> semcode_backend::query_type_hierarchy_supertypes(
	const std::string &filepath, int line, int character)
{
	if (semcode_cli_path_.empty()) {
		return {};
	}

	std::string identifier = extract_identifier_at(filepath, line, character);
	if (identifier.empty()) {
		return {};
	}

	std::string type_out = run_semcode_query(std::format("type {}", identifier));
	std::vector<location_info> locs;
	parse_file_and_line_locations(type_out, project_root_, locs);

	std::vector<type_hierarchy_item> items;
	for (const auto &loc : locs) {
		type_hierarchy_item item;
		item.name = identifier;
		item.kind = 5; // Class / Struct
		item.detail = "struct/type";
		item.uri = "file://" + loc.path;
		item.range = loc.range;
		item.selection_range = loc.range;
		items.push_back(std::move(item));
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

	bool is_c_cpp = (ext == ".cpp" || ext == ".c" || ext == ".h" || ext == ".hpp" ||
			 ext == ".cc" || ext == ".cxx");
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

std::string semcode_backend::run_semcode_query(const std::string &query) const
{
	if (semcode_cli_path_.empty() || project_root_.empty() || query.empty()) {
		return "";
	}

	// Check CLI memoization cache first to eliminate duplicate subprocess launches
	{
		std::lock_guard<std::mutex> lock(cli_cache_mutex_);
		auto it = cli_cache_.find(query);
		if (it != cli_cache_.end()) {
			return it->second;
		}
	}

	std::string cmd = std::format("{} -d {} --git-repo {} -q {}",
				      fs_utils::escape_shell_arg(semcode_cli_path_),
				      fs_utils::escape_shell_arg(project_root_),
				      fs_utils::escape_shell_arg(project_root_),
				      fs_utils::escape_shell_arg(query));

	std::string raw_output = fs_utils::execute_command_sync(cmd, 10);
	std::string sanitized = utf8::sanitize_terminal_output(raw_output);

	{
		std::lock_guard<std::mutex> lock(cli_cache_mutex_);
		cli_cache_[query] = sanitized;
	}

	return sanitized;
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
	return current_line.substr(start, end - start);
}
