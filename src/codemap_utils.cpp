#include "codemap_utils.h"

#include "project_manager.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <format>
#include <regex>
#include <sstream>
#include <string_view>
#include <thread>

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
			out.push_back({full_name, node.name, lsp_kind_to_string(node.kind), start, end, len, depth});
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

	static const std::regex func_regex(R"(^\s*(?:[\w:\<\>]+\s+)+([a-zA-Z_]\w*(?:::[a-zA-Z_]\w*)*)\s*\([^\)]*\)\s*(?:const|noexcept)?\s*\{?)");
	static const std::regex class_regex(R"(^\s*(?:class|struct)\s+([a-zA-Z_]\w*))");

	for (size_t i = 0; i < lines.size(); ++i) {
		int line_num = static_cast<int>(i + 1);
		std::smatch match;
		if (std::regex_search(lines[i], match, class_regex)) {
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
				out.push_back({match[1].str(), match[1].str(), "Class/Struct", line_num, end_line, len, 0});
			}
		} else if (std::regex_search(lines[i], match, func_regex)) {
			std::string name = match[1].str();
			if (name != "if" && name != "for" && name != "while" && name != "switch" && name != "catch") {
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
				int len = std::max(1, end_line - line_num + 1);
				if (len >= min_lines) {
					out.push_back({name, name, "Function", line_num, end_line, len, 0});
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

std::vector<codemap_symbol_info> get_document_codemap_symbols(const std::string &safe_path, agentlib::document_provider *doc_prov, int min_lines)
{
	std::vector<codemap_symbol_info> raw_symbols;

	// 1. Read document content if open in doc_provider, else disk
	std::string content;
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

std::vector<codemap_symbol_info> get_document_codemap_symbols(const std::string &safe_path, agentlib::tool_context &ctx, int min_lines)
{
	return get_document_codemap_symbols(safe_path, ctx.doc_provider, min_lines);
}

std::vector<codemap_symbol_info> get_document_codemap_symbols(const std::string &safe_path, int min_lines)
{
	return get_document_codemap_symbols(safe_path, static_cast<agentlib::document_provider *>(nullptr), min_lines);
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

		// 2. Search Relevance (recent grep patterns)
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

		// 3. Short symbol penalty (getters/setters < 3 lines)
		if (sym.line_count < 3) {
			score -= 5.0;
		}

		// 4. Deduplication penalty if previously reported
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

	size_t take_count = std::min(max_items, scored.size());
	res.selected_symbols.reserve(take_count);

	for (size_t i = 0; i < take_count; ++i) {
		res.selected_symbols.push_back(*scored[i].info);
		history.reported_symbol_names.insert(scored[i].info->name);
	}

	// Re-sort selected symbols by start_line ascending for document order display
	std::sort(res.selected_symbols.begin(), res.selected_symbols.end(), [](const codemap_symbol_info &a, const codemap_symbol_info &b) {
		return a.start_line < b.start_line;
	});

	res.omitted_count = (res.total_symbols > res.selected_symbols.size()) ? (res.total_symbols - res.selected_symbols.size()) : 0;
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

	size_t effective_total = (total_symbols_count > 0) ? total_symbols_count : symbols.size();

	std::stringstream ss;
	if (rich_format) {
		if (total_file_lines > 0) {
			if (effective_total > symbols.size()) {
				ss << std::format("### Codemap for `{}` (Top {} of {} symbols, {} lines):\n\n", display_path, symbols.size(), effective_total, total_file_lines);
			} else {
				ss << std::format("### Codemap for `{}` ({} symbols, {} lines):\n\n", display_path, effective_total, total_file_lines);
			}
		} else {
			if (effective_total > symbols.size()) {
				ss << std::format("### Codemap for `{}` (Top {} of {} symbols):\n\n", display_path, symbols.size(), effective_total);
			} else {
				ss << std::format("### Codemap for `{}` ({} symbols):\n\n", display_path, effective_total);
			}
		}
		ss << "| Symbol | Start Line | End Line | Lines |\n";
		ss << "| :--- | :---: | :---: | :---: |\n";
		for (const auto &sym : symbols) {
			ss << std::format("| `{}` | {} | {} | {} |\n", sym.display_name, sym.start_line, sym.end_line, sym.line_count);
		}
	} else {
		if (effective_total > symbols.size()) {
			ss << std::format("### Codemap for `{}` (Top {} of {} symbols):\n\n", display_path, symbols.size(), effective_total);
		} else {
			ss << std::format("### Codemap for `{}` ({} symbols):\n\n", display_path, symbols.size());
		}
		ss << "| Function | Start Line | End Line |\n";
		ss << "| :--- | :---: | :---: |\n";
		for (const auto &sym : symbols) {
			ss << std::format("| `{}` | {} | {} |\n", sym.display_name, sym.start_line, sym.end_line);
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

} // namespace tools
