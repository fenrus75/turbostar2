#include "codemap_utils.h"

#include "project_manager.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <format>
#include <regex>
#include <sstream>
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

std::vector<codemap_symbol_info> get_document_codemap_symbols(const std::string &safe_path, agentlib::tool_context &ctx, int min_lines)
{
	std::vector<codemap_symbol_info> raw_symbols;

	// 1. Read document content if open in doc_provider, else disk
	std::string content;
	if (ctx.doc_provider && ctx.doc_provider->get_open_document(safe_path)) {
		auto doc_snapshot = ctx.doc_provider->get_open_document(safe_path);
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

std::string format_codemap_table(const std::string &display_path, const std::vector<codemap_symbol_info> &symbols, bool rich_format, size_t total_file_lines)
{
	if (symbols.empty()) {
		return "";
	}

	std::stringstream ss;
	if (rich_format) {
		if (total_file_lines > 0) {
			ss << std::format("### Codemap for `{}` ({} symbols, {} lines):\n\n", display_path, symbols.size(), total_file_lines);
		} else {
			ss << std::format("### Codemap for `{}` ({} symbols):\n\n", display_path, symbols.size());
		}
		ss << "| Symbol | Start Line | End Line | Lines |\n";
		ss << "| :--- | :---: | :---: | :---: |\n";
		for (const auto &sym : symbols) {
			ss << std::format("| `{}` | {} | {} | {} |\n", sym.display_name, sym.start_line, sym.end_line, sym.line_count);
		}
	} else {
		ss << std::format("### Codemap for `{}`:\n\n", display_path);
		ss << "| Function | Start Line | End Line |\n";
		ss << "| :--- | :---: | :---: |\n";
		for (const auto &sym : symbols) {
			ss << std::format("| `{}` | {} | {} |\n", sym.display_name, sym.start_line, sym.end_line);
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
