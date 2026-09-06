#include "codemap_utils.h"
#include "event_logger.h"

#include "project_manager.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <format>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_set>
#include <re2/re2.h>
#include "config_manager.h"
#include "fs_utils.h"

namespace tools {

static std::string lsp_kind_to_string(int kind)
{
	switch (kind) {
	case 5: return "Class";
	case 6: return "Method";
	case 9: return "Enum";
	case 10: return "Interface";
	case 11: return "Function";
	case 12: return "Variable";
	case 23: return "Struct";
	case 26: return "TypeParameter";
	default: return "Symbol";
	}
}

static void collect_symbols_recursive(const lsp_manager::symbol_node &node, const std::string &prefix, int depth, int min_lines, std::vector<codemap_symbol_info> &out)
{
	std::string full_name = prefix.empty() ? node.name : prefix + "::" + node.name;
	int start = node.range.start_y + 1;
	int end = node.range.end_y + 1;
	int len = std::max(1, end - start + 1);

	// Only include functions, methods, classes, structs, enums, interfaces
	if (node.kind == 5 || node.kind == 6 || node.kind == 9 || node.kind == 10 || node.kind == 11 || node.kind == 23 || node.kind == 26 || prefix.empty()) {
		if (len >= min_lines) {
			out.push_back({full_name, node.name, lsp_kind_to_string(node.kind), start, end, len, depth, ""});
		}
	}

	for (const auto &child : node.children) {
		collect_symbols_recursive(child, full_name, depth + 1, min_lines, out);
	}
}

static std::vector<codemap_symbol_info> structure_symbol_hierarchy(const std::vector<codemap_symbol_info> &raw_symbols)
{
	std::vector<codemap_symbol_info> structured;
	std::unordered_map<std::string, size_t> class_node_indices;

	for (const auto &sym : raw_symbols) {
		codemap_symbol_info s = sym;
		size_t double_colon = s.name.rfind("::");

		if (double_colon != std::string::npos) {
			std::string owner_scope = s.name.substr(0, double_colon);
			std::string short_name = s.name.substr(double_colon + 2);

			size_t last_owner_sep = owner_scope.rfind("::");
			std::string class_name = (last_owner_sep != std::string::npos) ? owner_scope.substr(last_owner_sep + 2) : owner_scope;

			int parent_depth = s.depth;
			if (class_node_indices.find(owner_scope) == class_node_indices.end()) {
				codemap_symbol_info class_container;
				class_container.name = owner_scope;
				class_container.display_name = (parent_depth > 0) ? std::string(parent_depth * 4, ' ') + class_name : class_name;
				class_container.kind_str = "Class/Struct";
				class_container.start_line = s.start_line;
				class_container.end_line = s.end_line;
				class_container.line_count = s.line_count;
				class_container.depth = parent_depth;

				structured.push_back(class_container);
				class_node_indices[owner_scope] = structured.size() - 1;
			} else {
				size_t idx = class_node_indices[owner_scope];
				structured[idx].end_line = std::max(structured[idx].end_line, s.end_line);
				structured[idx].line_count = structured[idx].end_line - structured[idx].start_line + 1;
			}

			int child_depth = parent_depth + 1;
			s.display_name = std::string(child_depth * 4, ' ') + "::" + short_name;
			s.depth = child_depth;
			structured.push_back(s);
		} else {
			if (s.depth == 0) {
				s.display_name = s.name;
			} else {
				s.display_name = std::string(s.depth * 4, ' ') + s.name;
			}
			structured.push_back(s);
		}
	}

	return structured;
}

static void fallback_find_symbols(const std::string &safe_path, int min_lines, std::vector<codemap_symbol_info> &out)
{
	std::ifstream in(safe_path);
	if (!in.is_open())
		return;

	std::vector<std::string> lines;
	std::string l;
	while (std::getline(in, l)) {
		lines.push_back(l);
	}

	std::string ext = std::filesystem::path(safe_path).extension().string();
	for (char &c : ext) {
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	bool is_py = (ext == ".py");

	static const std::regex cpp_func_regex(R"(^\s*(?:[\w:\<\>]+\s+)+([a-zA-Z_]\w*(?:::[a-zA-Z_]\w*)*)\s*\([^\)]*\)\s*(?:const|noexcept)?\s*\{?)");
	static const std::regex cpp_class_regex(R"(^\s*(?:class|struct)\s+([a-zA-Z_]\w*))");
	static const std::regex py_func_regex(R"(^\s*def\s+([a-zA-Z_]\w*)\s*\()");
	static const std::regex py_class_regex(R"(^\s*class\s+([a-zA-Z_]\w*))");

	for (size_t i = 0; i < lines.size(); ++i) {
		int line_num = static_cast<int>(i + 1);
		std::smatch match;

		if (is_py) {
			if (std::regex_search(lines[i], match, py_class_regex) || std::regex_search(lines[i], match, py_func_regex)) {
				std::string name = match[1].str();
				out.push_back({name, name, "Function", line_num, line_num, 1, 0, ""});
			}
			continue;
		}

		if (std::regex_search(lines[i], match, cpp_class_regex)) {
			int end_line = line_num;
			int depth = 0;
			for (size_t j = i; j < lines.size(); ++j) {
				for (char c : lines[j]) {
					if (c == '{') depth++;
					else if (c == '}') depth--;
				}
				if (depth == 0 && j > i) {
					end_line = static_cast<int>(j + 1);
					break;
				}
			}
			int len = end_line - line_num + 1;
			if (len >= min_lines) {
				out.push_back({match[1].str(), match[1].str(), "Class/Struct", line_num, end_line, len, 0, ""});
			}
		} else if (std::regex_search(lines[i], match, cpp_func_regex)) {
			std::string name = match[1].str();
			if (name != "if" && name != "for" && name != "while" && name != "switch" && name != "catch") {
				std::string_view trimmed_line = lines[i];
				size_t last_non_ws = trimmed_line.find_last_not_of(" \t\r\n");
				if (last_non_ws != std::string_view::npos && trimmed_line[last_non_ws] == ';') {
					continue;
				}

				int end_line = line_num;
				int depth = 0;
				bool started = false;
				for (size_t j = i; j < lines.size(); ++j) {
					for (char c : lines[j]) {
						if (c == '{') { depth++; started = true; }
						else if (c == '}') { depth--; }
					}
					if (started && depth == 0) {
						end_line = static_cast<int>(j + 1);
						break;
					}
				}
				if (!started) {
					continue;
				}

				int len = std::max(1, end_line - line_num + 1);
				if (len >= min_lines) {
					out.push_back({name, name, "Function", line_num, end_line, len, 0, ""});
				}
			}
		}
	}
}

// Parses Markdown headings into a hierarchical codemap symbol list.
//
// Markdown has no (cheap/external) LSP dependency here, but its section structure is
// extremely predictable: lines starting with one or more '#' characters. We use these
// as the "symbols" so agents looking at a Markdown document get the same outline
// benefit that C++ files get from the LSP codemap. This is a tiny internal "mini-LSP".
//
// Heading level (number of leading '#') maps directly to outline depth, matching the
// depth/indentation that structure_symbol_hierarchy produces for nested code symbols.
static void parse_markdown_headings(const std::string &content, int min_lines, std::vector<codemap_symbol_info> &out)
{
	std::vector<codemap_symbol_info> headings;
	std::vector<int> stack; // heading levels forming the current ancestor chain

	std::istringstream ss(content);
	std::string line_text;
	int line_no = 0;
	while (std::getline(ss, line_text)) {
		++line_no;
		size_t first = line_text.find_first_not_of(" \t");
		if (first == std::string::npos || line_text[first] != '#') {
			continue;
		}
		// Count consecutive '#' to determine heading level.
		size_t level = 0;
		while (first + level < line_text.size() && line_text[first + level] == '#') {
			++level;
		}
		if (level == 0 || level > 6) {
			continue; // Not a valid ATX heading
		}
		// Must be followed by whitespace or end-of-line to be a heading (not a
		// stray '#' inside a paragraph).
		if (first + level < line_text.size() && line_text[first + level] != ' ' && line_text[first + level] != '\t') {
			continue;
		}
		// Heading text: strip leading whitespace after the '#', trim trailing spaces
		// and any trailing '#' set used for closing ATX headings (e.g. "## foo ##").
		size_t text_start = first + level;
		while (text_start < line_text.size() && (line_text[text_start] == ' ' || line_text[text_start] == '\t')) {
			++text_start;
		}
		std::string heading = line_text.substr(text_start);
		// Trim trailing whitespace. Also handle CRLF input (files read in binary mode
		// leave a trailing '\r'), so a bare '\r' is treated as whitespace.
		while (!heading.empty() && (heading.back() == ' ' || heading.back() == '\t' || heading.back() == '\r')) {
			heading.pop_back();
		}
		// Strip a closing ATX hash sequence ("## foo #####"), which CommonMark allows:
		// any run of trailing '#'s, as long as it's preceded by whitespace.
		if (heading.ends_with('#')) {
			size_t end_hashes = 0;
			while (end_hashes < heading.size() && heading[heading.size() - 1 - end_hashes] == '#') {
				++end_hashes;
			}
			// The hash run must be preceded by whitespace (or the very start) to count
			// as a closing sequence.
			if (end_hashes < heading.size() && (heading[heading.size() - 1 - end_hashes] == ' ' || heading[heading.size() - 1 - end_hashes] == '\t')) {
				heading.resize(heading.size() - end_hashes);
				while (!heading.empty() && (heading.back() == ' ' || heading.back() == '\t')) {
					heading.pop_back();
				}
			}
		}
		if (heading.empty()) {
			heading = line_text.substr(first, level);
		}

		// Compute depth as the heading level nested under shallower ancestors.
		while (!stack.empty() && stack.back() >= static_cast<int>(level)) {
			stack.pop_back();
		}
		int depth = static_cast<int>(stack.size());
		stack.push_back(static_cast<int>(level));

		headings.push_back({
			.name = heading,
			.display_name = (depth == 0) ? heading : std::string(depth * 4, ' ') + heading,
			.kind_str = "Heading",
			.start_line = line_no,
			.end_line = line_no,
			.line_count = 1,
			.depth = depth,
			.source_file = "",
		});
	}

	// Apply min_lines. Every heading is a 1-line anchor, so the behavior is
	// all-or-nothing: include the whole outline for min_lines <= 1, omit it entirely
	// when min_lines > 1 (matching how tiny C++ getters are pruned at higher
	// thresholds, and keeping the semantics simple/predictable).
	if (min_lines <= 1) {
		out.insert(out.end(), headings.begin(), headings.end());
	} else {
		for (const auto &h : headings) {
			if (h.line_count >= min_lines) {
				out.push_back(h);
			}
		}
	}
}

// Returns true if a file path is a Markdown document by extension.
static bool is_markdown_file(const std::string_view path)
{
	std::string ext = std::filesystem::path(path).extension().string();
	// Lowercase for case-insensitive matching.
	for (char &c : ext) {
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	return ext == ".md" || ext == ".markdown" || ext == ".mdown" || ext == ".mkd";
}

static std::vector<codemap_symbol_info> get_document_codemap_symbols_impl(const std::string &safe_path, agentlib::tool_context *ctx, agentlib::document_provider *doc_prov, int min_lines)
{
	std::vector<codemap_symbol_info> raw_symbols;

	// 1. Read document content if VFS URI, open in doc_provider, else disk
	std::string content;
	if (safe_path.find("://") != std::string::npos && ctx) {
		auto vfs = ctx->fs_security.get_vfs();
		if (vfs) {
			auto view_opt = vfs->read_file(safe_path);
			if (view_opt && *view_opt) {
				content = std::string((*view_opt)->view());
			}
		}
	}
	if (content.empty()) {
		if (doc_prov && doc_prov->get_open_document(safe_path)) {
			auto doc_snapshot = doc_prov->get_open_document(safe_path);
			size_t line_count = doc_snapshot->get_line_count();
			for (size_t i = 0; i < line_count; ++i) {
				content += doc_snapshot->get_line_text(i) + "\n";
			}
		} else {
			std::ifstream file(safe_path, std::ios::binary);
			if (file.is_open()) {
				content.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
			}
		}
	}

	// For Markdown, use our internal heading "mini-LSP" and skip the heavier generic
	// LSP machinery entirely (there is typically no Markdown language server installed).
	if (is_markdown_file(safe_path)) {
		parse_markdown_headings(content, min_lines, raw_symbols);
		std::sort(raw_symbols.begin(), raw_symbols.end(), [](const codemap_symbol_info &a, const codemap_symbol_info &b) {
			return a.start_line < b.start_line;
		});
		return raw_symbols;
	}

	// Synchronize buffer with LSP
	project_manager::get_instance().lsp_open_document(safe_path, content);

	// Query symbols (cached inside lsp_manager if unchanged)
	auto root_symbols = project_manager::get_instance().lsp_query_document_symbols(safe_path);
	if (root_symbols.empty()) {
		// Retry once if LSP was starting up
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		root_symbols = project_manager::get_instance().lsp_query_document_symbols(safe_path);
	}

	if (!root_symbols.empty()) {
		for (const auto &root : root_symbols) {
			collect_symbols_recursive(root, "", 0, min_lines, raw_symbols);
		}
	} else {
		fallback_find_symbols(safe_path, min_lines, raw_symbols);
	}

	// Sort raw symbols by start line
	std::sort(raw_symbols.begin(), raw_symbols.end(), [](const codemap_symbol_info &a, const codemap_symbol_info &b) {
		return a.start_line < b.start_line;
	});

	return structure_symbol_hierarchy(raw_symbols);
}

std::vector<codemap_symbol_info> get_document_codemap_symbols(const std::string &safe_path, agentlib::document_provider *doc_prov, int min_lines)
{
	return get_document_codemap_symbols_impl(safe_path, nullptr, doc_prov, min_lines);
}

std::vector<codemap_symbol_info> get_document_codemap_symbols(const std::string &safe_path, agentlib::tool_context &ctx, int min_lines)
{
	return get_document_codemap_symbols_impl(safe_path, &ctx, ctx.doc_provider, min_lines);
}

std::vector<codemap_symbol_info> get_document_codemap_symbols(const std::string &safe_path, int min_lines)
{
	return get_document_codemap_symbols_impl(safe_path, nullptr, nullptr, min_lines);
}

struct outgoing_call_cache_entry {
	std::filesystem::file_time_type last_mtime;
	std::chrono::steady_clock::time_point last_fetch_time;
	std::vector<outgoing_call_reference> calls;
};

static std::mutex g_outgoing_calls_cache_mutex;
static std::unordered_map<std::string, outgoing_call_cache_entry> g_outgoing_calls_cache;

static int get_symbol_name_column(const std::string &file_path, int start_line)
{
	std::ifstream file(file_path);
	if (!file.is_open()) return 0;

	std::string line;
	int current_line = 1;
	while (std::getline(file, line)) {
		if (current_line == start_line) {
			size_t paren_pos = line.find('(');
			if (paren_pos != std::string::npos && paren_pos > 0) {
				size_t col = paren_pos - 1;
				while (col > 0 && std::isspace(static_cast<unsigned char>(line[col]))) {
					col--;
				}
				return static_cast<int>(col);
			}
			size_t first_non_space = line.find_first_not_of(" \t");
			if (first_non_space != std::string::npos) {
				return static_cast<int>(first_non_space);
			}
			return 0;
		}
		current_line++;
	}
	return 0;
}

static void refresh_outgoing_calls_async(
	std::string safe_path,
	std::vector<codemap_symbol_info> doc_symbols,
	agentlib::tool_context *ctx)
{
	std::thread([safe_path = std::move(safe_path), doc_symbols = std::move(doc_symbols), ctx]() {
		try {
			std::error_code ec;
			auto current_mtime = std::filesystem::last_write_time(safe_path, ec);
			if (ec) return;

			std::vector<codemap_symbol_info> effective_symbols = doc_symbols;
			if (effective_symbols.empty()) {
				fallback_find_symbols(safe_path, 1, effective_symbols);
			}

			std::vector<std::pair<int, int>> positions;
			for (const auto &sym : effective_symbols) {
				int col = get_symbol_name_column(safe_path, sym.start_line);
				positions.push_back({sym.start_line - 1, col});
			}
			if (positions.empty()) {
				positions.push_back({0, 0});
			}
			if (positions.size() > 10) {
				positions.resize(10);
			}

			// Relaxed 10-second deadline for background indexing/completion
			auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
			auto lsp_items = project_manager::get_instance().lsp_query_call_hierarchy_outgoing_batch(safe_path, positions, deadline);
			if (lsp_items.empty()) return;

			std::vector<outgoing_call_reference> all_calls;
			for (const auto &item : lsp_items) {
				outgoing_call_reference ref;
				ref.caller_file = safe_path;
				ref.call_line = item.call_line + 1;
				ref.target_name = item.item.name;
				ref.target_kind = lsp_kind_to_string(item.item.kind);

				std::string target_uri_path = item.item.uri;
				if (target_uri_path.starts_with("file://")) {
					target_uri_path = target_uri_path.substr(7);
				}

				std::string resolved_impl_path = target_uri_path;
				if (ctx && (target_uri_path.ends_with(".h") || target_uri_path.ends_with(".hpp"))) {
					std::string impl = find_matching_impl_file(target_uri_path, *ctx);
					if (!impl.empty()) {
						resolved_impl_path = impl;
					}
				}

				ref.target_file = resolved_impl_path;
				ref.target_start_line = item.item.range.start_y + 1;
				ref.target_end_line = item.item.range.end_y + 1;

				if (resolved_impl_path != target_uri_path) {
					std::vector<codemap_symbol_info> impl_symbols;
					fallback_find_symbols(resolved_impl_path, 1, impl_symbols);
					const codemap_symbol_info *found = find_symbol_by_hint(impl_symbols, item.item.name);
					if (found) {
						ref.target_start_line = found->start_line;
						ref.target_end_line = found->end_line;
					}
				}

				all_calls.push_back(ref);
			}

			if (!all_calls.empty()) {
				std::lock_guard<std::mutex> lock(g_outgoing_calls_cache_mutex);
				g_outgoing_calls_cache[safe_path] = {current_mtime, std::chrono::steady_clock::now(), all_calls};
				event_logger::get_instance().log(std::format("LSP: background refresh outgoing calls path='{}' (found {} calls)", safe_path, all_calls.size()));
			}
		} catch (...) {}
	}).detach();
}

static bool is_project_file(const std::string &path, agentlib::tool_context * /*ctx*/ = nullptr)
{
	if (path.empty()) return false;
	if (path.starts_with("/usr/") || path.starts_with("/opt/") || path.starts_with("/lib/") ||
	    path.starts_with("/tmp/") || path.starts_with("/etc/") || path.starts_with("/var/")) {
		return false;
	}
	if (path.find("/include/") != std::string::npos || path.find("/bits/") != std::string::npos ||
	    path.find("gcc/") != std::string::npos || path.find("clang/") != std::string::npos) {
		return false;
	}

	// If absolute path, check if it resides within current project working directory
	if (path.starts_with("/")) {
		std::error_code ec;
		std::string cwd = std::filesystem::current_path(ec).string();
		if (!ec && !cwd.empty()) {
			if (!path.starts_with(cwd)) {
				return false;
			}
		}
	}
	return true;
}

static std::string normalize_display_path(const std::string &path, agentlib::tool_context * /*ctx*/ = nullptr)
{
	if (path.empty()) return path;
	std::string p = path;
	if (p.starts_with("file://")) {
		p = p.substr(7);
	}
	std::error_code ec;
	std::string cwd = std::filesystem::current_path(ec).string();
	if (!ec && !cwd.empty()) {
		if (!cwd.ends_with("/")) cwd += "/";
		if (p.starts_with(cwd)) {
			p = p.substr(cwd.length());
		}
	}
	return p;
}




std::vector<outgoing_call_reference> get_outgoing_calls_in_range(
	const std::string &safe_path,
	int start_line,
	int end_line,
	agentlib::tool_context *ctx,
	std::chrono::steady_clock::time_point deadline)
{
	std::vector<codemap_symbol_info> doc_symbols;
	if (ctx) {
		doc_symbols = get_document_codemap_symbols(safe_path, *ctx, 1);
	} else {
		doc_symbols = get_document_codemap_symbols(safe_path, 1);
	}
	return get_outgoing_calls_in_range(safe_path, start_line, end_line, doc_symbols, ctx, deadline);
}

std::vector<outgoing_call_reference> get_outgoing_calls_in_range(
	const std::string &safe_path,
	int start_line,
	int end_line,
	const std::vector<codemap_symbol_info> &doc_symbols,
	agentlib::tool_context *ctx,
	std::chrono::steady_clock::time_point deadline)
{
	if (std::chrono::steady_clock::now() >= deadline) {
		return {};
	}

	std::error_code ec;
	auto current_mtime = std::filesystem::last_write_time(safe_path, ec);

	{
		std::lock_guard<std::mutex> lock(g_outgoing_calls_cache_mutex);
		auto it = g_outgoing_calls_cache.find(safe_path);
		if (!ec && it != g_outgoing_calls_cache.end() && it->second.last_mtime == current_mtime && !it->second.calls.empty()) {
			std::vector<outgoing_call_reference> result;
			for (const auto &call : it->second.calls) {
				if (call.call_line >= start_line && call.call_line <= end_line) {
					result.push_back(call);
				}
			}
			auto now = std::chrono::steady_clock::now();
			if (now - it->second.last_fetch_time < std::chrono::seconds(5)) {
				return result;
			}
			// Stale cache hit (>= 5s): update last_fetch_time to avoid duplicate background requests
			// and return cached calls immediately while triggering background refresh.
			it->second.last_fetch_time = now;
			refresh_outgoing_calls_async(safe_path, doc_symbols, ctx);
			return result;
		}
	}

	std::vector<codemap_symbol_info> effective_symbols = doc_symbols;
	if (effective_symbols.empty()) {
		fallback_find_symbols(safe_path, 1, effective_symbols);
	}

	std::vector<std::pair<int, int>> positions;
	for (const auto &sym : effective_symbols) {
		if (sym.start_line <= end_line && sym.end_line >= start_line) {
			int col = get_symbol_name_column(safe_path, sym.start_line);
			positions.push_back({sym.start_line - 1, col});
		}
	}

	if (positions.empty() && start_line > 0) {
		int col = get_symbol_name_column(safe_path, start_line);
		positions.push_back({start_line - 1, col});
	}

	if (positions.size() > 10) {
		positions.resize(10);
	}

	std::vector<outgoing_call_reference> all_calls;

	auto lsp_items = project_manager::get_instance().lsp_query_call_hierarchy_outgoing_batch(safe_path, positions, deadline);

	if (!lsp_items.empty()) {
		for (const auto &item : lsp_items) {
			if (std::chrono::steady_clock::now() >= deadline)
				break;
			outgoing_call_reference ref;
			ref.caller_file = safe_path;
			ref.call_line = item.call_line + 1;
			ref.target_name = item.item.name;
			ref.target_kind = lsp_kind_to_string(item.item.kind);
			ref.is_direct_read_call = (ref.call_line >= start_line && ref.call_line <= end_line);

			std::string target_uri_path = item.item.uri;
			if (target_uri_path.starts_with("file://")) {
				target_uri_path = target_uri_path.substr(7);
			}

			std::string resolved_impl_path = target_uri_path;
			if (ctx && (target_uri_path.ends_with(".h") || target_uri_path.ends_with(".hpp"))) {
				std::string impl = find_matching_impl_file(target_uri_path, *ctx);
				if (!impl.empty()) {
					resolved_impl_path = impl;
				}
			}

			ref.target_file = resolved_impl_path;
			ref.target_start_line = item.item.range.start_y + 1;
			ref.target_end_line = item.item.range.end_y + 1;

			if (resolved_impl_path != target_uri_path) {
				std::vector<codemap_symbol_info> impl_symbols;
				fallback_find_symbols(resolved_impl_path, 1, impl_symbols);
				const codemap_symbol_info *found = find_symbol_by_hint(impl_symbols, item.item.name);
				if (found) {
					ref.target_start_line = found->start_line;
					ref.target_end_line = found->end_line;
				}
			}

			all_calls.push_back(ref);
		}
	}

	if (!ec && !all_calls.empty()) {
		std::lock_guard<std::mutex> lock(g_outgoing_calls_cache_mutex);
		g_outgoing_calls_cache[safe_path] = {current_mtime, std::chrono::steady_clock::now(), all_calls};
	} else if (lsp_items.empty()) {
		// Synchronous lookup timed out or returned empty: launch background refresh thread with 10s deadline
		// so if clangd finishes after the synchronous deadline, the result is saved to cache for the next call!
		refresh_outgoing_calls_async(safe_path, doc_symbols, ctx);
	}

	std::vector<outgoing_call_reference> range_result;
	for (const auto &call : all_calls) {
		if (call.call_line >= start_line && call.call_line <= end_line) {
			range_result.push_back(call);
		}
	}

	event_logger::get_instance().log(
		std::format("get_outgoing_calls_in_range: path='{}', range={}-{} using {} (found {} calls)",
			safe_path, start_line, end_line, lsp_items.empty() ? "NO_CALLS" : "LSP_BATCH", range_result.size()));

	return range_result;
}

std::vector<outgoing_call_reference> get_outgoing_calls_for_symbol(
	const std::string &safe_path,
	std::string_view symbol_name,
	agentlib::tool_context *ctx)
{
	std::vector<codemap_symbol_info> doc_symbols;
	if (ctx) {
		doc_symbols = get_document_codemap_symbols(safe_path, *ctx, 1);
	} else {
		doc_symbols = get_document_codemap_symbols(safe_path, 1);
	}

	const codemap_symbol_info *sym = find_symbol_by_hint(doc_symbols, symbol_name);
	if (!sym) {
		return {};
	}

	return get_outgoing_calls_in_range(safe_path, sym->start_line, sym->end_line, ctx);
}

codemap_selection_result select_prioritized_codemap_symbols(
	const std::vector<codemap_symbol_info> &all_symbols,
	int read_start,
	int read_end,
	const std::string &safe_path,
	agentlib::tool_context &ctx,
	size_t max_items)
{
	codemap_selection_result res;
	res.total_symbols = all_symbols.size();
	if (all_symbols.empty()) {
		return res;
	}

	// Check mtime invalidation on codemap history
	std::error_code ec;
	auto current_mtime = std::filesystem::last_write_time(safe_path, ec);
	auto &history = ctx.codemap_history[safe_path];
	if (!ec && history.last_mtime != current_mtime) {
		history.reported_symbol_names.clear();
		history.last_mtime = current_mtime;
	}

	// LRU eviction cap if history gets too large
	if (ctx.codemap_history.size() > 64) {
		ctx.codemap_history.clear();
		ctx.codemap_history[safe_path] = history;
	}

	// Query outgoing calls inside the read line range and enclosing function scope (bound total codemap LSP latency to 1100ms)
	auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1100);
	auto direct_outgoing_calls = get_outgoing_calls_in_range(safe_path, read_start, read_end, all_symbols, &ctx, deadline);
	std::unordered_set<std::string> direct_call_targets;
	std::unordered_set<std::string> enclosing_call_targets;

	const codemap_symbol_info *enclosing_sym = find_enclosing_symbol(all_symbols, read_start);
	if (!enclosing_sym) {
		enclosing_sym = find_enclosing_symbol(all_symbols, read_end);
	}

	if (enclosing_sym) {
		auto enclosing_calls = get_outgoing_calls_in_range(safe_path, enclosing_sym->start_line, enclosing_sym->end_line, all_symbols, &ctx, deadline);
		for (const auto &call : enclosing_calls) {
			enclosing_call_targets.insert(call.target_name);
		}
	}

	for (const auto &call : direct_outgoing_calls) {
		direct_call_targets.insert(call.target_name);
	}

	struct scored_symbol {
		const codemap_symbol_info *info;
		double score;
		size_t original_index;
	};

	std::vector<scored_symbol> scored;
	scored.reserve(all_symbols.size());

	for (size_t idx = 0; idx < all_symbols.size(); ++idx) {
		const auto &sym = all_symbols[idx];
		double score = 0.0;

		// 1. Boundary & Enclosing Scopes
		bool encloses_start = (read_start >= sym.start_line && read_start <= sym.end_line);
		bool encloses_end = (read_end >= sym.start_line && read_end <= sym.end_line);
		bool encloses_entire = (sym.start_line <= read_start && sym.end_line >= read_end);
		bool is_internal = (sym.start_line >= read_start && sym.end_line <= read_end);

		if (encloses_start || encloses_end) {
			score += 10.0;
		} else if (encloses_entire) {
			score += 8.0;
		} else if (is_internal) {
			score += 1.0;
		} else {
			// Proximity check (within 50 lines)
			int dist_start = std::abs(sym.start_line - read_start);
			int dist_end = std::abs(sym.end_line - read_end);
			if (dist_start <= 50 || dist_end <= 50) {
				score += 3.0;
			}
		}

		// 2. Outgoing Call Boost (+30.0 for direct read calls, +15.0 for enclosing scope calls)
		size_t double_colon = sym.name.rfind("::");
		std::string short_sym_name = (double_colon != std::string::npos) ? sym.name.substr(double_colon + 2) : sym.name;

		if (direct_call_targets.contains(sym.name) || direct_call_targets.contains(short_sym_name)) {
			score += 30.0;
		} else if (enclosing_call_targets.contains(sym.name) || enclosing_call_targets.contains(short_sym_name)) {
			score += 15.0;
		}

		// 3. Search Relevance (recent grep patterns)
		double grep_weight = 5.0;
		for (const auto &pattern : ctx.recent_grep_patterns) {
			if (!pattern.empty()) {
				std::string name_lower = sym.name;
				std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				std::string pat_lower = pattern;
				std::transform(pat_lower.begin(), pat_lower.end(), pat_lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

				if (name_lower.find(pat_lower) != std::string::npos || pat_lower.find(name_lower) != std::string::npos) {
					score += grep_weight;
					break;
				}
			}
			grep_weight /= 2.0; // Decaying weight for older search terms
		}

		// 4. Short symbol penalty (getters/setters < 3 lines)
		if (sym.line_count < 3) {
			score -= 5.0;
		}

		// 5. Deduplication penalty if previously reported
		if (history.reported_symbol_names.contains(sym.name)) {
			score -= 3.0;
		}

		scored.push_back({&sym, score, idx});
	}

	// Sort by score descending; break ties by start_line ascending
	std::stable_sort(scored.begin(), scored.end(), [](const scored_symbol &a, const scored_symbol &b) {
		if (a.score != b.score) {
			return a.score > b.score;
		}
		return a.info->start_line < b.info->start_line;
	});

	size_t primary_max = std::min<size_t>(10, max_items);
	size_t take_count = std::min(primary_max, scored.size());
	res.selected_symbols.reserve(take_count + 10);

	for (size_t i = 0; i < take_count; ++i) {
		res.selected_symbols.push_back(*scored[i].info);
		history.reported_symbol_names.insert(scored[i].info->name);
	}

	// Re-sort primary file symbols by start_line ascending for document order display
	std::sort(res.selected_symbols.begin(), res.selected_symbols.end(), [](const codemap_symbol_info &a, const codemap_symbol_info &b) {
		return a.start_line < b.start_line;
	});

	// Append up to 10 cross-file outgoing dependency call symbols under Option D
	size_t cross_file_count = 0;
	std::unordered_set<std::string> added_cross_file_keys;
	std::string norm_safe_path = normalize_display_path(safe_path, &ctx);

	for (const auto &call : direct_outgoing_calls) {
		if (cross_file_count >= 10)
			break;

		if (!call.target_file.empty() && call.target_start_line > 0 && is_project_file(call.target_file, &ctx)) {
			std::string norm_target = normalize_display_path(call.target_file, &ctx);
			if (norm_target == norm_safe_path)
				continue;

			std::string key = norm_target + ":" + call.target_name;
			if (added_cross_file_keys.contains(key))
				continue;
			added_cross_file_keys.insert(key);

			codemap_symbol_info dep_sym;
			dep_sym.name = call.target_name;
			dep_sym.display_name = call.target_name;
			dep_sym.kind_str = call.target_kind.empty() ? "Function" : call.target_kind;
			dep_sym.start_line = call.target_start_line;
			dep_sym.end_line = call.target_end_line;
			dep_sym.line_count = std::max(1, call.target_end_line - call.target_start_line + 1);
			dep_sym.depth = 0;
			dep_sym.source_file = norm_target;

			res.selected_symbols.push_back(dep_sym);
			cross_file_count++;
		}
	}

	res.omitted_count = (res.total_symbols > take_count) ? (res.total_symbols - take_count) : 0;
	return res;
}

std::string format_codemap_table(
	const std::string &display_path,
	const std::vector<codemap_symbol_info> &symbols,
	bool rich_format,
	size_t total_file_lines,
	size_t total_symbols_count,
	size_t omitted_count,
	agentlib::tool_context *ctx)
{
	if (symbols.empty()) {
		return "";
	}

	// Separate primary file symbols from cross-file dependency symbols (Option D)
	std::vector<codemap_symbol_info> primary_symbols;
	std::unordered_map<std::string, std::vector<codemap_symbol_info>> dependency_symbols;
	std::string norm_primary = normalize_display_path(display_path, ctx);

	for (const auto &sym : symbols) {
		std::string norm_source = normalize_display_path(sym.source_file, ctx);
		if (norm_source.empty() || norm_source == norm_primary) {
			primary_symbols.push_back(sym);
		} else {
			dependency_symbols[norm_source].push_back(sym);
		}
	}

	size_t effective_total = (total_symbols_count > 0) ? total_symbols_count : primary_symbols.size();

	std::stringstream ss;
	if (rich_format) {
		if (!primary_symbols.empty()) {
			if (total_file_lines > 0) {
				if (effective_total > primary_symbols.size()) {
					ss << std::format("### Codemap for `{}` (Top {} of {} symbols, {} lines):\n\n", display_path, primary_symbols.size(), effective_total, total_file_lines);
				} else {
					ss << std::format("### Codemap for `{}` (Full {} symbols, {} lines):\n\n", display_path, effective_total, total_file_lines);
				}
			} else {
				if (effective_total > primary_symbols.size()) {
					ss << std::format("### Codemap for `{}` (Top {} of {} symbols):\n\n", display_path, primary_symbols.size(), effective_total);
				} else {
					ss << std::format("### Codemap for `{}` (Full {} symbols):\n\n", display_path, effective_total);
				}
			}
			ss << "| Symbol | Start Line | End Line | Lines |\n";
			ss << "| :--- | :---: | :---: | :---: |\n";
			for (const auto &sym : primary_symbols) {
				ss << std::format("| `{}` | {} | {} | {} |\n", sym.display_name, sym.start_line, sym.end_line, sym.line_count);
			}
		}
	} else {
		if (!primary_symbols.empty()) {
			if (effective_total > primary_symbols.size()) {
				ss << std::format("### Codemap for `{}` (Top {} of {} symbols):\n\n", display_path, primary_symbols.size(), effective_total);
			} else {
				ss << std::format("### Codemap for `{}` ({} symbols):\n\n", display_path, primary_symbols.size());
			}
			ss << "| Function | Start Line | End Line |\n";
			ss << "| :--- | :---: | :---: |\n";
			for (const auto &sym : primary_symbols) {
				ss << std::format("| `{}` | {} | {} |\n", sym.display_name, sym.start_line, sym.end_line);
			}
		}
	}

	if (omitted_count > 0) {
		if (ctx && !ctx->has_hinted_fs_file_codemap) {
			ss << std::format("*... [{} other symbols omitted (use fs_file_codemap if full symbol table is needed)]*\n", omitted_count);
			ctx->has_hinted_fs_file_codemap = true;
		} else {
			ss << std::format("*... [{} other symbols omitted]*\n", omitted_count);
		}
	}

	// Render Option D secondary dependency codemap sections
	for (const auto &[dep_path, dep_syms] : dependency_symbols) {
		ss << std::format("\n### Codemap for `{}` (Called Dependency):\n\n", dep_path);
		if (rich_format) {
			ss << "| Symbol | Start Line | End Line | Lines |\n";
			ss << "| :--- | :---: | :---: | :---: |\n";
			for (const auto &sym : dep_syms) {
				ss << std::format("| `{}` | {} | {} | {} |\n", sym.display_name, sym.start_line, sym.end_line, sym.line_count);
			}
		} else {
			ss << "| Function | Start Line | End Line |\n";
			ss << "| :--- | :---: | :---: |\n";
			for (const auto &sym : dep_syms) {
				ss << std::format("| `{}` | {} | {} |\n", sym.display_name, sym.start_line, sym.end_line);
			}
		}
	}

	ss << "\n";
	return ss.str();
}

std::string find_matching_impl_file(const std::string &header_path, agentlib::tool_context &/*ctx*/)
{
	std::filesystem::path hp(header_path);
	std::string ext = hp.extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

	if (ext != ".h" && ext != ".hpp" && ext != ".hh" && ext != ".hxx") {
		return "";
	}

	std::vector<std::string> candidates = {
		(hp.parent_path() / (hp.stem().string() + ".cpp")).string(),
		(hp.parent_path() / (hp.stem().string() + ".c")).string(),
		(hp.parent_path() / (hp.stem().string() + ".cc")).string(),
		(hp.parent_path() / (hp.stem().string() + ".cxx")).string()
	};

	std::error_code ec;
	for (const auto &cand : candidates) {
		if (std::filesystem::exists(cand, ec)) {
			return cand;
		}
	}

	return "";
}

std::string find_matching_header_file(const std::string &impl_path, agentlib::tool_context &/*ctx*/)
{
	std::filesystem::path ip(impl_path);
	std::string ext = ip.extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

	if (ext != ".cpp" && ext != ".c" && ext != ".cc" && ext != ".cxx") {
		return "";
	}

	std::vector<std::string> candidates = {
		(ip.parent_path() / (ip.stem().string() + ".h")).string(),
		(ip.parent_path() / (ip.stem().string() + ".hpp")).string(),
		(ip.parent_path() / (ip.stem().string() + ".hh")).string(),
		(ip.parent_path() / (ip.stem().string() + ".hxx")).string()
	};

	std::error_code ec;
	for (const auto &cand : candidates) {
		if (std::filesystem::exists(cand, ec) && is_project_file(cand) && fs_utils::is_regular_file(cand)) {
			return cand;
		}
	}

	return "";
}

static bool is_ident_char(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

static bool contains_identifier(std::string_view text, std::string_view ident)
{
	if (ident.empty() || text.empty())
		return false;
	size_t pos = 0;
	while ((pos = text.find(ident, pos)) != std::string_view::npos) {
		bool left_ok = (pos == 0 || !is_ident_char(text[pos - 1]));
		bool right_ok = (pos + ident.size() >= text.size() || !is_ident_char(text[pos + ident.size()]));
		if (left_ok && right_ok) {
			return true;
		}
		pos += ident.size();
	}
	return false;
}

static const lsp_manager::symbol_node* find_class_symbol_node(
	const std::vector<lsp_manager::symbol_node> &nodes,
	std::string_view target_class_name)
{
	for (const auto &node : nodes) {
		if ((node.kind == 5 || node.kind == 23) && node.name == target_class_name) {
			return &node;
		}
		auto child_match = find_class_symbol_node(node.children, target_class_name);
		if (child_match) {
			return child_match;
		}
	}
	return nullptr;
}

std::string extract_class_context_preview(
	const std::string &cpp_path,
	int start_line,
	int end_line,
	const std::vector<std::string> &read_lines,
	agentlib::tool_context &ctx)
{
	std::filesystem::path ip(cpp_path);
	std::string ext = ip.extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
	if (ext != ".cpp" && ext != ".c" && ext != ".cc" && ext != ".cxx") {
		return "";
	}
	if (read_lines.empty()) {
		return "";
	}

	auto symbols = get_document_codemap_symbols(cpp_path, ctx, 1);
	const codemap_symbol_info *enclosing = find_enclosing_symbol(symbols, start_line);
	if (!enclosing && end_line > start_line) {
		enclosing = find_enclosing_symbol(symbols, end_line);
	}
	if (!enclosing) {
		for (int l = start_line; l <= end_line; ++l) {
			enclosing = find_enclosing_symbol(symbols, l);
			if (enclosing) break;
		}
	}

	std::string simple_class_name;
	std::string method_name;
	if (enclosing) {
		std::string enclosing_name = enclosing->name;
		size_t last_colons = enclosing_name.rfind("::");
		if (last_colons != std::string::npos) {
			std::string class_scope = enclosing_name.substr(0, last_colons);
			method_name = enclosing_name.substr(last_colons + 2);
			size_t prev_colons = class_scope.rfind("::");
			simple_class_name = (prev_colons != std::string::npos) ? class_scope.substr(prev_colons + 2) : class_scope;
		} else if (enclosing->kind_str == "Class" || enclosing->kind_str == "Struct" || enclosing->kind_str == "Class/Struct") {
			simple_class_name = enclosing_name;
		}
	}

	if (simple_class_name.empty()) {
		static const re2::RE2 class_method_re(R"(([A-Za-z_][A-Za-z0-9_]*)::([A-Za-z_][A-Za-z0-9_]*))");
		std::string c_name, m_name;
		for (const auto &line : read_lines) {
			if (re2::RE2::PartialMatch(line, class_method_re, &c_name, &m_name)) {
				if (c_name != "std" && c_name != "boost" && c_name != "tools" && c_name != "agentlib" && c_name != "re2" && c_name != "fs_utils") {
					simple_class_name = c_name;
					method_name = m_name;
					break;
				}
			}
		}
	}

	if (simple_class_name.empty()) {
		return "";
	}

	// Locate header file using LSP definition query
	std::string header_path;
	int lsp_line = -1;
	int lsp_col = -1;

	for (size_t i = 0; i < read_lines.size(); ++i) {
		size_t pos = read_lines[i].find(simple_class_name + "::");
		if (pos != std::string::npos) {
			lsp_line = (start_line + static_cast<int>(i)) - 1;
			lsp_col = static_cast<int>(pos);
			break;
		}
	}

	if (lsp_line < 0 && enclosing && enclosing->start_line > 0) {
		std::string line_content;
		if (ctx.doc_provider && ctx.doc_provider->get_open_document(cpp_path)) {
			auto doc = ctx.doc_provider->get_open_document(cpp_path);
			if (enclosing->start_line <= static_cast<int>(doc->get_line_count())) {
				line_content = doc->get_line_text(enclosing->start_line - 1);
			}
		} else {
			std::ifstream f(cpp_path);
			std::string l;
			int cur = 1;
			while (cur <= enclosing->start_line && std::getline(f, l)) {
				if (cur == enclosing->start_line) {
					line_content = l;
					break;
				}
				cur++;
			}
		}
		size_t pos = line_content.find(simple_class_name + "::");
		if (pos != std::string::npos) {
			lsp_line = enclosing->start_line - 1;
			lsp_col = static_cast<int>(pos);
		}
	}

	if (lsp_line >= 0) {
		auto locs = project_manager::get_instance().lsp_query_definition(cpp_path, lsp_line, lsp_col);
		for (const auto &loc : locs) {
			std::filesystem::path lp(loc.path);
			std::string lext = lp.extension().string();
			std::transform(lext.begin(), lext.end(), lext.begin(), ::tolower);
			if (lext == ".h" || lext == ".hpp" || lext == ".hh" || lext == ".hxx") {
				header_path = loc.path;
				break;
			}
		}
	}

	if (header_path.empty()) {
		header_path = find_matching_header_file(cpp_path, ctx);
	}

	if (header_path.empty()) {
		return "";
	}

	if (!is_project_file(header_path) || !fs_utils::is_regular_file(header_path)) {
		return "";
	}

	std::string safe_header_path = header_path;

	std::vector<std::string> header_lines;
	if (ctx.doc_provider && ctx.doc_provider->get_open_document(safe_header_path)) {
		auto doc = ctx.doc_provider->get_open_document(safe_header_path);
		size_t count = doc->get_line_count();
		header_lines.reserve(count);
		for (size_t i = 0; i < count; ++i) {
			header_lines.push_back(doc->get_line_text(i));
		}
	} else {
		std::ifstream f(safe_header_path);
		if (!f.is_open()) return "";
		std::string l;
		while (std::getline(f, l)) {
			if (!l.empty() && l.back() == '\r') l.pop_back();
			header_lines.push_back(l);
		}
	}

	if (header_lines.empty()) {
		return "";
	}

	struct member_item {
		std::string name;
		int kind;
		int start_line;
		int end_line;
	};

	std::vector<member_item> candidate_fields;
	std::vector<member_item> candidate_methods;

	std::string header_content;
	for (const auto &l : header_lines) {
		header_content += l;
		header_content += "\n";
	}
	project_manager::get_instance().lsp_open_document(safe_header_path, header_content);
	auto root_symbols = project_manager::get_instance().lsp_query_document_symbols(safe_header_path);
	const lsp_manager::symbol_node *class_node = find_class_symbol_node(root_symbols, simple_class_name);

	if (class_node && !class_node->children.empty()) {
		for (const auto &child : class_node->children) {
			int start = child.range.start_y + 1;
			int end = child.range.end_y + 1;
			if (child.kind == 8 || child.kind == 13 || child.kind == 7) {
				candidate_fields.push_back({child.name, child.kind, start, end});
			} else if (child.kind == 6 || child.kind == 12 || child.kind == 9) {
				candidate_methods.push_back({child.name, child.kind, start, end});
			}
		}
	} else if (class_node && class_node->children.empty()) {
		// When clangd returns flat SymbolInformation instead of hierarchical DocumentSymbol,
		// root_symbols contains symbols whose line ranges fall within class_node's range.
		int class_start_y = class_node->range.start_y;
		int class_end_y = class_node->range.end_y;
		for (const auto &sym : root_symbols) {
			if (&sym == class_node) continue;
			if (sym.range.start_y >= class_start_y && sym.range.end_y <= class_end_y) {
				int start = sym.range.start_y + 1;
				int end = sym.range.end_y + 1;
				if (sym.kind == 8 || sym.kind == 13 || sym.kind == 7) {
					candidate_fields.push_back({sym.name, sym.kind, start, end});
				} else if (sym.kind == 6 || sym.kind == 12 || sym.kind == 9) {
					candidate_methods.push_back({sym.name, sym.kind, start, end});
				}
			}
		}
	}

	// Hybrid strategy: If LSP produced zero fields or zero methods, run the fast header parser
	// to fill in any missing members without duplicating already detected symbols.
	if (candidate_fields.empty() || candidate_methods.empty()) {
		auto has_field = [&](std::string_view name) {
			return std::any_of(candidate_fields.begin(), candidate_fields.end(),
					   [&](const auto &item) { return item.name == name; });
		};
		auto has_method = [&](std::string_view name) {
			return std::any_of(candidate_methods.begin(), candidate_methods.end(),
					   [&](const auto &item) { return item.name == name; });
		};

		re2::RE2 class_decl_re(std::format(R"(\b(?:class|struct)\s+{}\b)", simple_class_name));
		int brace_depth = 0;
		bool in_class = false;

		for (size_t i = 0; i < header_lines.size(); ++i) {
			const auto &line = header_lines[i];
			int line_num = static_cast<int>(i + 1);
			if (!in_class) {
				std::string_view trimmed_check = line;
				size_t first_non_ws = trimmed_check.find_first_not_of(" \t");
				if (first_non_ws != std::string_view::npos) {
					trimmed_check.remove_prefix(first_non_ws);
					if (trimmed_check.starts_with("//") || trimmed_check.starts_with("/*") || trimmed_check.starts_with("*")) {
						continue;
					}
					// Ignore forward declarations like "class document;"
					if (trimmed_check.find(';') != std::string_view::npos && trimmed_check.find('{') == std::string_view::npos) {
						continue;
					}
				}
				if (re2::RE2::PartialMatch(line, class_decl_re)) {
					in_class = true;
					for (char c : line) {
						if (c == '{') brace_depth++;
						else if (c == '}') brace_depth--;
					}
				}
			} else {
				for (char c : line) {
					if (c == '{') brace_depth++;
					else if (c == '}') {
						brace_depth--;
						if (brace_depth <= 0) {
							in_class = false;
							break;
						}
					}
				}
				if (in_class && brace_depth == 1) {
					std::string_view trimmed = line;
					size_t first = trimmed.find_first_not_of(" \t");
					if (first != std::string_view::npos) {
						trimmed.remove_prefix(first);
						if (trimmed.starts_with("//") || trimmed.starts_with("/*") || trimmed.starts_with("*")) {
							continue;
						}
						if (trimmed.starts_with("public:") || trimmed.starts_with("protected:") || trimmed.starts_with("private:")) {
							continue;
						}
						size_t sc = trimmed.find(';');
						if (sc != std::string_view::npos) {
							std::string_view decl = trimmed.substr(0, sc);
							size_t paren = decl.find('(');
							if (paren != std::string_view::npos) {
								size_t name_end = decl.find_last_not_of(" \t", paren - 1);
								if (name_end != std::string_view::npos) {
									size_t name_start = decl.find_last_of(" \t*&", name_end);
									std::string mname = std::string(decl.substr(name_start == std::string_view::npos ? 0 : name_start + 1, name_end - (name_start == std::string_view::npos ? 0 : name_start + 1) + 1));
									if (!mname.empty() && is_ident_char(mname[0]) && !has_method(mname)) {
										candidate_methods.push_back({mname, 6, line_num, line_num});
									}
								}
							} else {
								size_t eq = decl.find('{');
								if (eq == std::string_view::npos) eq = decl.find('=');
								std::string_view var_part = (eq != std::string_view::npos) ? decl.substr(0, eq) : decl;
								size_t name_end = var_part.find_last_not_of(" \t");
								if (name_end != std::string_view::npos) {
									size_t name_start = var_part.find_last_of(" \t*&", name_end);
									std::string vname = std::string(var_part.substr(name_start == std::string_view::npos ? 0 : name_start + 1, name_end - (name_start == std::string_view::npos ? 0 : name_start + 1) + 1));
									if (!vname.empty() && is_ident_char(vname[0]) && !has_field(vname)) {
										candidate_fields.push_back({vname, 8, line_num, line_num});
									}
								}
							}
						}
					}
				}
			}
		}
	}

	std::string combined_read_text;
	for (const auto &l : read_lines) {
		combined_read_text += l;
		combined_read_text += "\n";
	}

	std::vector<member_item> used_fields;
	std::vector<member_item> used_methods;

	for (const auto &f : candidate_fields) {
		if (used_fields.size() >= 10) break;
		if (contains_identifier(combined_read_text, f.name)) {
			used_fields.push_back(f);
		}
	}

	for (const auto &m : candidate_methods) {
		if (used_methods.size() >= 5) break;
		if (contains_identifier(combined_read_text, m.name)) {
			used_methods.push_back(m);
		}
	}

	if (used_fields.empty() && used_methods.empty()) {
		return "";
	}

	std::sort(used_fields.begin(), used_fields.end(), [](const member_item &a, const member_item &b) {
		return a.start_line < b.start_line;
	});
	std::sort(used_methods.begin(), used_methods.end(), [](const member_item &a, const member_item &b) {
		return a.start_line < b.start_line;
	});

	std::string display_header_path = normalize_display_path(safe_header_path, &ctx);
	std::stringstream ss;
	ss << std::format("### Class Context: `{}` (extracted from `{}` for referenced members):\n",
			  simple_class_name, display_header_path);
	ss << "```cpp\n";

	if (!used_fields.empty()) {
		ss << "// Referenced member variables:\n";
		std::set<int> emitted_lines;
		for (const auto &f : used_fields) {
			for (int ln = f.start_line; ln <= f.end_line && ln <= static_cast<int>(header_lines.size()); ++ln) {
				if (emitted_lines.insert(ln).second) {
					ss << std::format("{}: {}\n", ln, header_lines[ln - 1]);
				}
			}
		}
	}

	if (!used_methods.empty()) {
		if (!used_fields.empty()) {
			ss << "\n";
		}
		ss << "// Referenced member functions:\n";
		std::set<int> emitted_lines;
		for (const auto &m : used_methods) {
			int max_l = std::min(m.end_line, m.start_line + 2);
			for (int ln = m.start_line; ln <= max_l && ln <= static_cast<int>(header_lines.size()); ++ln) {
				if (emitted_lines.insert(ln).second) {
					ss << std::format("{}: {}\n", ln, header_lines[ln - 1]);
				}
			}
		}
	}

	ss << "```\n";
	return ss.str();
}

const codemap_symbol_info* find_enclosing_symbol(const std::vector<codemap_symbol_info> &symbols, int line_number)
{
	const codemap_symbol_info *best_match = nullptr;
	int smallest_span = std::numeric_limits<int>::max();

	for (const auto &sym : symbols) {
		if (line_number >= sym.start_line && line_number <= sym.end_line) {
			int span = sym.end_line - sym.start_line + 1;
			if (span < smallest_span) {
				smallest_span = span;
				best_match = &sym;
			}
		}
	}
	return best_match;
}

const codemap_symbol_info* find_symbol_by_hint(const std::vector<codemap_symbol_info> &symbols, std::string_view hint)
{
	if (hint.empty())
		return nullptr;

	// 1. Exact match against name or display_name (without leading spaces)
	for (const auto &sym : symbols) {
		std::string_view clean_display = sym.display_name;
		size_t first_non_space = clean_display.find_first_not_of(" \t:");
		if (first_non_space != std::string_view::npos) {
			clean_display.remove_prefix(first_non_space);
		}
		if (sym.name == hint || clean_display == hint) {
			return &sym;
		}
	}

	// 2. Substring match against name or display_name
	for (const auto &sym : symbols) {
		if (sym.name.find(hint) != std::string::npos || sym.display_name.find(hint) != std::string::npos) {
			return &sym;
		}
	}

	return nullptr;
}

std::string get_line_symbol_annotation(const std::vector<codemap_symbol_info> &symbols, int line_number)
{
	const codemap_symbol_info *sym = find_enclosing_symbol(symbols, line_number);
	if (!sym) {
		return "";
	}
	return std::format("[symbol: {} (lines {}-{})]", sym->name, sym->start_line, sym->end_line);
}

std::string get_line_symbol_annotation(const std::string &safe_path, int line_number, agentlib::tool_context *ctx)
{
	std::vector<codemap_symbol_info> symbols;
	if (ctx) {
		symbols = get_document_codemap_symbols(safe_path, *ctx, 1);
	} else {
		symbols = get_document_codemap_symbols(safe_path, 1);
	}
	return get_line_symbol_annotation(symbols, line_number);
}

std::string augment_compiler_output_with_codemap(const std::string &output, agentlib::tool_context *ctx, size_t max_annotations)
{
	if (output.empty() || max_annotations == 0) {
		return output;
	}

	static const re2::RE2 error_regex(R"(^(.*?):([0-9]+):([0-9]+): (error|warning): (.*)$)");
	std::stringstream ss(output);
	std::string line;
	std::string result;
	result.reserve(output.size());

	size_t count = 0;
	std::unordered_map<std::string, std::vector<codemap_symbol_info>> file_symbols_cache;
	std::unordered_set<std::string> seen_locations;

	while (std::getline(ss, line)) {
		std::string file_match, severity_match, message_match;
		int line_num = 0, col_num = 0;

		if (count < max_annotations && re2::RE2::PartialMatch(line, error_regex, &file_match, &line_num, &col_num, &severity_match, &message_match)) {
			std::filesystem::path p(file_match);
			if (!p.is_absolute()) {
				std::string build_dir = config_manager::get_instance().get_build_directory();
				std::filesystem::path build_p = fs_utils::safe_absolute(std::filesystem::path(build_dir) / p);
				std::error_code ec;
				if (std::filesystem::exists(build_p, ec) && !ec) {
					p = build_p;
				} else {
					p = fs_utils::safe_absolute(p);
				}
			}
			std::string abs_path = p.string();
			std::string loc_key = std::format("{}:{}", abs_path, line_num);

			if (!seen_locations.contains(loc_key)) {
				seen_locations.insert(loc_key);

				if (!file_symbols_cache.contains(abs_path)) {
					if (ctx) {
						file_symbols_cache[abs_path] = get_document_codemap_symbols(abs_path, *ctx, 1);
					} else {
						file_symbols_cache[abs_path] = get_document_codemap_symbols(abs_path, 1);
					}
				}

				std::string annotation = get_line_symbol_annotation(file_symbols_cache[abs_path], line_num);
				if (!annotation.empty()) {
					line += " " + annotation;
					count++;
				}
			}
		}

		result += line;
		result += "\n";
	}

	if (!output.empty() && output.back() != '\n' && !result.empty() && result.back() == '\n') {
		result.pop_back();
	}

	return result;
}

} // namespace tools
