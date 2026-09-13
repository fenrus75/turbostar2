#include "semcode_backend.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include "event_logger.h"
#include "codemap_utils.h"
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
					int end_line_num = -1;
					if (dash != std::string_view::npos) {
						std::string_view end_part = l.substr(dash + 1);
						size_t end_non_ws = end_part.find_first_not_of(" \t");
						if (end_non_ws != std::string_view::npos) {
							try {
								end_line_num = std::stoi(std::string(end_part.substr(end_non_ws)));
							} catch (...) {
							}
						}
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
						int zero_start = std::max(0, line_num - 1);
						int zero_end = (end_line_num >= line_num) ? std::max(0, end_line_num - 1) : zero_start;
						loc.range = text_range{zero_start, 0, zero_end, 0};
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

	// If semcode reports no exact match found, or only regex fallback matches, treat as 0 exact locations (unambiguous empty).
	if (text.find("No exact match found") != std::string_view::npos || text.find("(regex matches)") != std::string_view::npos ||
	    text.find("No results found") != std::string_view::npos) {
		event_logger::get_instance().log("parse_definition_locations_strict_unique: 'No exact match found' / '(regex matches)' "
						 "detected -> 0 exact locations (true)");
		return true;
	}

	// Check for semcode multi-definition banner: e.g. "Found 5 function definitions with name ..."
	if (text.find(" definitions with name ") != std::string_view::npos) {
		event_logger::get_instance().log(
		    "parse_definition_locations_strict_unique: multi-definition banner detected -> ambiguous (false)");
		return false;
	}

	std::istringstream stream{std::string(text)};
	std::string line_str;
	std::string current_file;
	std::string current_kind;
	std::string current_underlying;

	while (std::getline(stream, line_str)) {
		while (!line_str.empty() && (line_str.back() == '\r' || line_str.back() == ' ')) {
			line_str.pop_back();
		}
		size_t first = line_str.find_first_not_of(" \t");
		if (first == std::string::npos) {
			continue;
		}
		std::string_view trimmed = std::string_view(line_str).substr(first);

		if (trimmed.starts_with("=== Typedef Information ===")) {
			current_kind = "typedef";
		} else if (trimmed.starts_with("Name:")) {
			if (trimmed.find("typedef") != std::string_view::npos) {
				current_kind = "typedef";
			} else if (trimmed.find("struct") != std::string_view::npos) {
				current_kind = "struct";
			} else if (trimmed.find("class") != std::string_view::npos) {
				current_kind = "class";
			} else if (trimmed.find("enum") != std::string_view::npos) {
				current_kind = "enum";
			}
		} else if (trimmed.starts_with("Underlying Type:") || trimmed.starts_with("Underlying type:") ||
			   trimmed.starts_with("// Underlying type:") || trimmed.starts_with("// Underlying Type:")) {
			size_t colon = trimmed.find(':');
			std::string_view ut = trimmed.substr(colon + 1);
			size_t ut_first = ut.find_first_not_of(" \t");
			size_t ut_last = ut.find_last_not_of(" \t\r\n");
			if (ut_first != std::string_view::npos && ut_last != std::string_view::npos && ut_first <= ut_last) {
				current_underlying = std::string(ut.substr(ut_first, ut_last - ut_first + 1));
				current_kind = "typedef";
				if (!out.empty()) {
					out.back().underlying_type = current_underlying;
					out.back().kind = "typedef";
				}
			}
		} else if (trimmed.starts_with("File:")) {
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
					int end_line_num = -1;
					if (dash != std::string_view::npos) {
						std::string_view end_part = l.substr(dash + 1);
						size_t end_non_ws = end_part.find_first_not_of(" \t");
						if (end_non_ws != std::string_view::npos) {
							try {
								end_line_num = std::stoi(std::string(end_part.substr(end_non_ws)));
							} catch (...) {
							}
						}
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
						int zero_start = std::max(0, line_num - 1);
						int zero_end = (end_line_num >= line_num) ? std::max(0, end_line_num - 1) : zero_start;
						loc.range = text_range{zero_start, 0, zero_end, 0};
						loc.kind = current_kind;
						loc.underlying_type = current_underlying;

						// Check if identical location was already parsed (e.g. semcode emitting both
						// 'Type Information' and 'Typedef Information' where the second is a refinement of the
						// first).
						bool already_present = false;
						for (auto &existing : out) {
							if (existing.path == loc.path && existing.range.start_y == loc.range.start_y) {
								already_present = true;
								if (loc.range.end_y > existing.range.end_y) {
									existing.range.end_y = loc.range.end_y;
								}
								if (!loc.kind.empty()) {
									existing.kind = loc.kind;
								}
								if (!loc.underlying_type.empty()) {
									existing.underlying_type = loc.underlying_type;
								}
								break;
							}
						}
						if (!already_present) {
							out.push_back(std::move(loc));
						}
					} catch (...) {
					}
				}
				current_file.clear();
			}
		}
	}

	if (out.size() > 1) {
		event_logger::get_instance().log(
		    std::format("parse_definition_locations_strict_unique: out.size() = {} > 1 -> ambiguous (false)", out.size()));
		out.clear();
		return false;
	}

	event_logger::get_instance().log(std::format("parse_definition_locations_strict_unique: parsed {} unique location(s)", out.size()));
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
	std::string current_detail;
	std::string current_file;
	int current_line = -1;

	auto flush_current = [&]() {
		if (current_callee.empty()) {
			return;
		}
		lsp_backend::call_hierarchy_item item;
		item.name = current_callee;
		item.detail = current_detail;
		if (!current_file.empty()) {
			fs::path full_path = fs::path(current_file);
			if (!full_path.is_absolute() && !root.empty()) {
				full_path = fs::path(root) / full_path;
			}
			item.uri = "file://" + full_path.string();
		}
		int zero_line = (current_line > 0) ? (current_line - 1) : -1;
		item.range = text_range{zero_line, 0, zero_line, 0};
		item.selection_range = text_range{zero_line, 0, zero_line, 0};
		out.push_back(std::move(item));

		current_callee.clear();
		current_detail.clear();
		current_file.clear();
		current_line = -1;
	};

	while (std::getline(stream, line_str)) {
		while (!line_str.empty() && (line_str.back() == '\r' || line_str.back() == ' ')) {
			line_str.pop_back();
		}
		size_t first = line_str.find_first_not_of(" \t");
		if (first == std::string::npos) {
			continue;
		}
		std::string_view trimmed = std::string_view(line_str).substr(first);

		// Check for arrow format: "→ name" or "-> name"
		std::string_view arrow_name;
		if (trimmed.starts_with("\xe2\x86\x92")) { // UTF-8 '→'
			arrow_name = trimmed.substr(3);
		} else if (trimmed.starts_with("->")) {
			arrow_name = trimmed.substr(2);
		}

		if (!arrow_name.empty()) {
			flush_current();
			size_t nw = arrow_name.find_first_not_of(" \t");
			if (nw != std::string_view::npos) {
				current_callee = std::string(arrow_name.substr(nw));
			}
			continue;
		}

		// Check for numbered format: "1. name"
		if (std::isdigit(static_cast<unsigned char>(trimmed.front()))) {
			size_t dot = trimmed.find('.');
			if (dot != std::string_view::npos) {
				flush_current();
				std::string_view name_part = trimmed.substr(dot + 1);
				size_t nw = name_part.find_first_not_of(" \t");
				if (nw != std::string_view::npos) {
					current_callee = std::string(name_part.substr(nw));
				}
				continue;
			}
		}

		// Check for detail / location line: "void (file:line) ..."
		if (!current_callee.empty()) {
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
						current_line = std::stoi(std::string(line_part));
						current_file = std::string(file_part);
						current_detail = std::string(detail);
						flush_current();
					} catch (...) {
					}
				}
			}
		}
	}

	flush_current();
}

semcode_backend::semcode_backend(std::string project_root) : project_root_(std::move(project_root))
{
	if (project_root_.empty()) {
		project_root_ = project_manager::get_instance().get_project_root();
	}
	semcode_lsp_path_ = find_semcode_lsp();
	semcode_cli_path_ = find_semcode_cli();

	event_logger::get_instance().log("semcode_backend initialized for root '{}' (semcode-lsp: '{}', semcode-cli: '{}')", project_root_,
					 semcode_lsp_path_, semcode_cli_path_);
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
		if (!type_output.empty() &&
		    (type_output.find("=== Type Information ===") != std::string::npos ||
		     type_output.find("Type Definition:") != std::string::npos || type_output.find("Fields:") != std::string::npos)) {
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

		if (hover_stopping_.load(std::memory_order_relaxed) || req.request_id != hover_counter_.load(std::memory_order_relaxed)) {
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

void semcode_backend::request_document_highlight(const std::string & /*filepath*/, int /*line*/, int /*character*/)
{
	// semcode does not track in-memory AST token highlights; gracefully no-op
}

void semcode_backend::request_selection_range(const std::string & /*filepath*/, int /*line*/, int /*character*/)
{
	// semcode does not compute semantic selection ranges; gracefully no-op
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
				event_logger::get_instance().log(
				    std::format("semcode_backend::query_definition: identifier='{}', ambiguous func definitions, aborting",
						identifier));
				return {};
			}

			if (!results.empty()) {
				std::string norm_caller = fs_utils::make_relative_to_project(filepath);
				std::string norm_res = fs_utils::make_relative_to_project(results[0].path);
				if (norm_res.starts_with("arch/") && !norm_caller.starts_with("arch/")) {
					event_logger::get_instance().log(std::format(
						"semcode_backend::query_definition: rejecting cross-arch result '{}' for caller '{}'",
						norm_res, norm_caller));
					return {};
				}
				event_logger::get_instance().log(
				    std::format("semcode_backend::query_definition: identifier='{}', found {} locations via func",
						identifier, results.size()));
				return results;
			}

			event_logger::get_instance().log(std::format(
			    "semcode_backend::query_definition: identifier='{}', no func definition found, checking type", identifier));

			// If not a function, check if it is a type definition
			std::string type_out = run_semcode_query(std::format("type {}", identifier));
			bool type_ok = parse_definition_locations_strict_unique(type_out, project_root_, results);
			if (!type_ok) {
				// Ambiguity detected: multiple type definitions exist, fail immediately
				event_logger::get_instance().log(std::format(
				    "semcode_backend::query_definition: identifier='{}', ambiguous type definitions", identifier));
				return {};
			}

			if (!results.empty()) {
				event_logger::get_instance().log(
				    std::format("semcode_backend::query_definition: identifier='{}', found {} locations via type",
						identifier, results.size()));
				return results;
			}

			event_logger::get_instance().log(
			    std::format("semcode_backend::query_definition: identifier='{}', no type definition found", identifier));
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

std::vector<lsp_backend::location_info> semcode_backend::query_type_definition(const std::string &filepath, int line, int character)
{
	std::vector<location_info> results;

	// 1. Primary resolution: query semcode CLI directly for type definition
	if (!semcode_cli_path_.empty()) {
		std::string identifier = extract_identifier_at(filepath, line, character);
		if (!identifier.empty()) {
			std::string type_out = run_semcode_query(std::format("type {}", identifier));
			bool type_ok = parse_definition_locations_strict_unique(type_out, project_root_, results);
			if (!type_ok) {
				event_logger::get_instance().log(std::format(
				    "semcode_backend::query_type_definition: identifier='{}', ambiguous type definitions", identifier));
				return {};
			}

			if (!results.empty()) {
				event_logger::get_instance().log(
				    std::format("semcode_backend::query_type_definition: identifier='{}', found {} locations via type",
						identifier, results.size()));
				return results;
			}

			event_logger::get_instance().log(
			    std::format("semcode_backend::query_type_definition: identifier='{}', no type definition found", identifier));
		}
		return {};
	}

	// 2. Fallback if semcode CLI binary is not available: JSON-RPC query via semcode-lsp
	results = standard_lsp_backend::query_type_definition(filepath, line, character);
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

	// 2. Hybrid fallback: query semcode CLI for symbol callers
	if (!semcode_cli_path_.empty()) {
		std::string identifier = extract_identifier_at(filepath, line, character);
		if (!identifier.empty()) {
			std::string callers_out = run_semcode_query(std::format("callers -v {}", identifier));
			parse_callers_locations(callers_out, project_root_, results);
			return results;
		}
	}

	return {};
}

std::vector<lsp_backend::symbol_info> semcode_backend::query_workspace_symbols(const std::string &query)
{
	if (semcode_cli_path_.empty()) {
		return standard_lsp_backend::query_workspace_symbols(query);
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

	event_logger::get_instance().log(
	    std::format("semcode_backend::query_workspace_symbols: query='{}', found {} symbols (func={}, type={})", query, symbols.size(),
			func_locs.size(), type_locs.size()));

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

std::vector<lsp_backend::call_hierarchy_item> semcode_backend::query_call_hierarchy_outgoing(const std::string &filepath, int line,
											     int character,
											     std::chrono::steady_clock::time_point deadline)
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
	if (items.empty()) {
		std::string plain_calls = run_semcode_query(std::format("calls {}", identifier));
		parse_calls_hierarchy(plain_calls, project_root_, items);
	}

	event_logger::get_instance().log(
	    std::format("semcode_backend::query_call_hierarchy_outgoing: identifier='{}', found {} raw calls from semcode CLI", identifier,
			items.size()));

	std::vector<call_hierarchy_item> filtered_items;
	filtered_items.reserve(items.size());
	std::string norm_caller = fs_utils::make_relative_to_project(filepath);

	for (auto &item : items) {
		if (std::chrono::steady_clock::now() > deadline) {
			break;
		}
		if (item.name.empty()) {
			continue;
		}

		std::string item_path = fs_utils::make_relative_to_project(item.uri);

		// 1. Cross-architecture filtering: reject candidate if it points to arch/ and caller is not in that arch
		if (item_path.starts_with("arch/")) {
			if (!norm_caller.starts_with("arch/")) {
				event_logger::get_instance().log(std::format(
					"semcode_backend::query_call_hierarchy_outgoing: filtering cross-arch callee '{}' in '{}' (caller='{}')",
					item.name, item_path, norm_caller));
				continue;
			}
			size_t caller_arch_slash = norm_caller.find('/', 5);
			size_t cand_arch_slash = item_path.find('/', 5);
			if (caller_arch_slash != std::string::npos && cand_arch_slash != std::string::npos) {
				if (norm_caller.substr(0, caller_arch_slash) != item_path.substr(0, cand_arch_slash)) {
					event_logger::get_instance().log(std::format(
						"semcode_backend::query_call_hierarchy_outgoing: filtering mismatched arch callee '{}' in '{}' (caller='{}')",
						item.name, item_path, norm_caller));
					continue;
				}
			}
		}

		// 2. Strict unique definition filtering: query semcode CLI 'func <name>'
		// If multiple definitions exist (e.g. memset having multiple definitions across archs),
		// parse_definition_locations_strict_unique returns false.
		std::string func_out = run_semcode_query(std::format("func {}", item.name));
		std::vector<location_info> locs;
		bool func_ok = parse_definition_locations_strict_unique(func_out, project_root_, locs);
		if (!func_ok) {
			event_logger::get_instance().log(std::format(
				"semcode_backend::query_call_hierarchy_outgoing: filtering ambiguous callee '{}'", item.name));
			continue;
		}

		if (locs.size() == 1) {
			std::string loc_path = fs_utils::make_relative_to_project(locs[0].path);
			if (loc_path.starts_with("arch/")) {
				if (!norm_caller.starts_with("arch/")) {
					event_logger::get_instance().log(std::format(
						"semcode_backend::query_call_hierarchy_outgoing: filtering cross-arch definition '{}' for '{}' (caller='{}')",
						loc_path, item.name, norm_caller));
					continue;
				}
				size_t caller_arch_slash = norm_caller.find('/', 5);
				size_t cand_arch_slash = loc_path.find('/', 5);
				if (caller_arch_slash != std::string::npos && cand_arch_slash != std::string::npos) {
					if (norm_caller.substr(0, caller_arch_slash) != loc_path.substr(0, cand_arch_slash)) {
						continue;
					}
				}
			}
			item.uri = "file://" + locs[0].path;
			item.range = locs[0].range;
			item.selection_range = locs[0].range;
		}

		filtered_items.push_back(std::move(item));
	}

	event_logger::get_instance().log(
	    std::format("semcode_backend::query_call_hierarchy_outgoing: identifier='{}', retained {}/{} calls after filtering",
			identifier, filtered_items.size(), items.size()));

	return filtered_items;
}

std::vector<lsp_backend::outgoing_call_item>
semcode_backend::query_call_hierarchy_outgoing_batch(const std::string &filepath, const std::vector<std::pair<int, int>> &positions,
						     std::chrono::steady_clock::time_point deadline)
{
	std::vector<outgoing_call_item> batch_results;
	for (const auto &[line, character] : positions) {
		if (std::chrono::steady_clock::now() > deadline) {
			break;
		}
		auto calls = query_call_hierarchy_outgoing(filepath, line, character, deadline);
		for (auto &call : calls) {
			outgoing_call_item out_item;
			out_item.call_line = line;
			out_item.item = std::move(call);
			batch_results.push_back(std::move(out_item));
		}
	}
	return batch_results;
}

std::vector<lsp_backend::type_hierarchy_item> semcode_backend::query_type_hierarchy_supertypes(const std::string &filepath, int line,
											       int character)
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

	std::string cmd = std::format("{} -d {} --git-repo {} -q {}", fs_utils::escape_shell_arg(semcode_cli_path_),
				      fs_utils::escape_shell_arg(project_root_), fs_utils::escape_shell_arg(project_root_),
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
