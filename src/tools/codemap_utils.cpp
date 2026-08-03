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

static void collect_symbols_recursive(const lsp_manager::symbol_node &node, const std::string &prefix, std::vector<codemap_symbol_info> &out)
{
	std::string full_name = prefix.empty() ? node.name : prefix + "::" + node.name;
	int start = node.range.start_y + 1;
	int end = node.range.end_y + 1;
	int len = std::max(1, end - start + 1);

	// Only include functions, methods, classes, structs, enums, interfaces
	if (node.kind == 5 || node.kind == 6 || node.kind == 9 || node.kind == 10 || node.kind == 11 || node.kind == 23 || node.kind == 26 || prefix.empty()) {
		out.push_back({full_name, lsp_kind_to_string(node.kind), start, end, len});
	}

	for (const auto &child : node.children) {
		collect_symbols_recursive(child, full_name, out);
	}
}

static void fallback_find_symbols(const std::string &safe_path, std::vector<codemap_symbol_info> &out)
{
	std::ifstream in(safe_path);
	if (!in.is_open())
		return;

	std::string line;
	int line_num = 0;
	static const std::regex func_regex(R"(^\s*(?:[\w:\<\>]+\s+)+([a-zA-Z_]\w*(?:::[a-zA-Z_]\w*)*)\s*\([^\)]*\)\s*(?:const|noexcept)?\s*\{?)");
	static const std::regex class_regex(R"(^\s*(?:class|struct)\s+([a-zA-Z_]\w*))");

	std::smatch match;
	while (std::getline(in, line)) {
		line_num++;
		if (std::regex_search(line, match, class_regex)) {
			out.push_back({match[1].str(), "Class/Struct", line_num, line_num, 1});
		} else if (std::regex_search(line, match, func_regex)) {
			std::string name = match[1].str();
			if (name != "if" && name != "for" && name != "while" && name != "switch" && name != "catch") {
				out.push_back({name, "Function", line_num, line_num, 1});
			}
		}
	}
}

std::vector<codemap_symbol_info> get_document_codemap_symbols(const std::string &safe_path, agentlib::tool_context &ctx)
{
	std::vector<codemap_symbol_info> symbols;

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
			collect_symbols_recursive(root, "", symbols);
		}
	} else {
		fallback_find_symbols(safe_path, symbols);
	}

	// Sort symbols by start line
	std::sort(symbols.begin(), symbols.end(), [](const codemap_symbol_info &a, const codemap_symbol_info &b) {
		return a.start_line < b.start_line;
	});

	return symbols;
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
		ss << "| Symbol | Kind | Start Line | End Line | Lines |\n";
		ss << "| :--- | :--- | :---: | :---: | :---: |\n";
		for (const auto &sym : symbols) {
			ss << std::format("| `{}` | {} | {} | {} | {} |\n", sym.name, sym.kind_str, sym.start_line, sym.end_line, sym.line_count);
		}
	} else {
		ss << std::format("### Codemap for `{}`:\n\n", display_path);
		ss << "| Function | Start Line | End Line |\n";
		ss << "| :--- | :---: | :---: |\n";
		for (const auto &sym : symbols) {
			ss << std::format("| `{}` | {} | {} |\n", sym.name, sym.start_line, sym.end_line);
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

} // namespace tools
