#include <algorithm>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>
#include "../../agentlib/document_provider.h"
#include "../../agentlib/interactions/action.h"
#include "../../agentlib/virtual_file_system.h"
#include "../../project_manager.h"
#include "fs_read_symbol.h"

namespace tools
{

namespace
{

void find_symbols_recursive(const lsp_manager::symbol_node &node, const std::string &parent_scope,
			    const std::string &target_name, std::vector<lsp_manager::symbol_node> &matches,
			    bool is_python)
{
	std::string sep = is_python ? "." : "::";
	std::string current_scope = parent_scope.empty() ? node.name : (parent_scope + sep + node.name);

	if (node.name == target_name || 
	    current_scope == target_name || 
	    (current_scope.length() >= target_name.length() + sep.length() && 
	     current_scope.substr(current_scope.length() - target_name.length() - sep.length()) == sep + target_name)) {
		matches.push_back(node);
	}

	for (const auto &child : node.children) {
		find_symbols_recursive(child, current_scope, target_name, matches, is_python);
	}
}

size_t count_max_consecutive_backticks(const std::vector<std::string> &lines)
{
	size_t max_count = 0;
	for (const auto &line : lines) {
		size_t current_count = 0;
		for (char c : line) {
			if (c == '`') {
				current_count++;
				max_count = std::max(max_count, current_count);
			} else {
				current_count = 0;
			}
		}
	}
	return max_count;
}

std::string get_language_from_extension(const std::string &path)
{
	std::filesystem::path p(path);
	std::string ext = p.extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
	if (ext == ".cpp" || ext == ".h" || ext == ".hpp" || ext == ".c" || ext == ".cc") {
		return "cpp";
	}
	if (ext == ".py") {
		return "python";
	}
	return "";
}

size_t count_total_lines(const std::string &path, agentlib::tool_context &ctx)
{
	if (ctx.doc_provider && ctx.doc_provider->get_open_document(path)) {
		return ctx.doc_provider->get_open_document(path)->get_line_count();
	}
	if (path.find("://") != std::string::npos) {
		auto vfs = ctx.fs_security.get_vfs();
		if (vfs) {
			auto view_opt = vfs->read_file(path);
			if (view_opt) {
				std::string_view view = view_opt.value()->view();
				size_t total = std::count(view.begin(), view.end(), '\n');
				if (!view.empty() && view.back() != '\n') {
					total++;
				}
				return total;
			}
		}
		return 0;
	}
	std::ifstream file(path, std::ios::binary);
	if (file.is_open()) {
		size_t total = std::count(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>(), '\n');
		file.clear();
		file.seekg(0, std::ios::end);
		auto size = file.tellg();
		if (size > 0) {
			file.clear();
			file.seekg(-1, std::ios_base::end);
			char last_char;
			file.get(last_char);
			if (last_char != '\n') {
				total++;
			}
		}
		return total;
	}
	return 0;
}

} // namespace

fs_read_symbol_tool::fs_read_symbol_tool(fs_read_symbol_args args) : args_(std::move(args))
{
	interaction_ = std::make_shared<agentlib::interaction_action>(args_.requested_path + " -> Reading symbol " + args_.symbol_name);
}

std::shared_ptr<agentlib::agent_interaction> fs_read_symbol_tool::get_interaction() const
{
	return interaction_;
}

bool fs_read_symbol_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string &out_error) const
{
	if (!project_manager::get_instance().lsp_is_supported_file(args_.safe_path)) {
		out_error = "LSP is not supported for file: " + args_.requested_path;
		return false;
	}
	return true;
}

std::string fs_read_symbol_tool::execute(agentlib::tool_context &ctx)
{
	ctx.file_drift_tracker.erase(args_.safe_path);

	// 1. Read document content to feed to didOpen notification
	std::string content;
	if (ctx.doc_provider && ctx.doc_provider->get_open_document(args_.safe_path)) {
		auto doc_snapshot = ctx.doc_provider->get_open_document(args_.safe_path);
		size_t line_count = doc_snapshot->get_line_count();
		for (size_t i = 0; i < line_count; ++i) {
			content += doc_snapshot->get_line_text(i) + "\n";
		}
	} else {
		std::ifstream file(args_.safe_path, std::ios::binary);
		if (file.is_open()) {
			content.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		}
	}

	// 2. Open document in LSP server to synchronize buffer
	project_manager::get_instance().lsp_open_document(args_.safe_path, content);

	// 3. Query symbols, retrying up to 5 times with 200ms sleep to allow clangd indexer startup/catchup
	std::vector<lsp_manager::symbol_node> root_symbols;
	for (int attempt = 0; attempt < 5; ++attempt) {
		root_symbols = project_manager::get_instance().lsp_query_document_symbols(args_.safe_path);
		if (!root_symbols.empty()) {
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
	}

	if (root_symbols.empty()) {
		return "Error: No symbols returned by LSP. Either clangd is still initializing, or there are no functions/classes defined in this file.";
	}

	// 4. Find all matching symbols
	bool is_py = get_language_from_extension(args_.safe_path) == "python";
	std::vector<lsp_manager::symbol_node> matches;
	for (const auto &root : root_symbols) {
		find_symbols_recursive(root, "", args_.symbol_name, matches, is_py);
	}

	if (matches.empty()) {
		return "Error: Symbol '" + args_.symbol_name + "' was not found in '" + args_.requested_path + "'.";
	}

	// 5. Read lines for each match and compile output formatted similarly to fs_read_lines
	std::stringstream ss;
	size_t total_lines = count_total_lines(args_.safe_path, ctx);

	for (size_t i = 0; i < matches.size(); ++i) {
		const auto &match = matches[i];
		// Expand the range by 2 lines on either side
		int start = std::max(1, match.range.start_y + 1 - 2);
		int end = std::min(static_cast<int>(total_lines), match.range.end_y + 1 + 2);

		auto lines = read_lines(args_.safe_path, start, end, ctx);

		size_t max_backticks = count_max_consecutive_backticks(lines);
		size_t fence_len = std::max<size_t>(3, max_backticks + 1);
		std::string fence(fence_len, '`');
		std::string lang = get_language_from_extension(args_.requested_path);

		if (matches.size() > 1) {
			ss << "### Match " << (i + 1) << ": Symbol '" << match.name << "'\n";
		}
		ss << "Code for lines " << start << " - " << end << " of " << args_.requested_path
		   << " (total " << total_lines << " lines):\n"
		   << fence << lang << "\n";
		int current_line = start;
		for (const auto &line : lines) {
			ss << current_line << ": " << line << "\n";
			current_line++;
		}
		ss << fence << "\n\n";
	}

	return ss.str();
}

std::vector<std::string> fs_read_symbol_tool::read_lines(const std::string &path, int start, int end, agentlib::tool_context &ctx) const
{
	std::vector<std::string> lines;

	if (ctx.doc_provider && ctx.doc_provider->get_open_document(path)) {
		auto doc = ctx.doc_provider->get_open_document(path);
		int total = static_cast<int>(doc->get_line_count());
		int start_idx = std::max(0, start - 1);
		int end_idx = std::min(total - 1, end - 1);
		for (int i = start_idx; i <= end_idx; ++i) {
			lines.push_back(doc->get_line_text(i));
		}
		return lines;
	}

	if (path.find("://") != std::string::npos) {
		auto vfs = ctx.fs_security.get_vfs();
		if (vfs) {
			auto view_opt = vfs->read_file(path);
			if (view_opt) {
				std::string_view view = view_opt.value()->view();
				int current_line = 1;
				size_t start_pos = 0;
				while (start_pos < view.length()) {
					size_t end_pos = view.find('\n', start_pos);
					std::string_view line = (end_pos == std::string_view::npos) ? view.substr(start_pos) : view.substr(start_pos, end_pos - start_pos);
					if (current_line >= start && current_line <= end) {
						lines.emplace_back(line);
					} else if (current_line > end) {
						break;
					}
					start_pos = (end_pos == std::string_view::npos) ? view.length() : end_pos + 1;
					current_line++;
				}
			}
		}
		return lines;
	}

	std::ifstream file(path, std::ios::binary);
	if (file.is_open()) {
		std::string line;
		int current_line = 1;
		while (current_line < start && std::getline(file, line)) {
			current_line++;
		}
		while (current_line <= end && std::getline(file, line)) {
			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}
			lines.push_back(line);
			current_line++;
		}
	}
	return lines;
}

} // namespace tools
