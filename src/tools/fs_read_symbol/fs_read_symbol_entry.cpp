#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>
#include <format>
#include "mime.h"
#include "agentlib/document_provider.h"
#include "agentlib/interactions/action.h"
#include "agentlib/virtual_file_system.h"
#include "codemap_utils.h"
#include "project_manager.h"
#include "fs_read_symbol.h"
#include "fs_utils.h"
#include "mapped_file.h"
#include "utf8.h"
namespace tools
{

namespace
{

bool matches_codemap_symbol(const codemap_symbol_info &sym, std::string_view target_name, bool is_python)
{
	if (sym.name == target_name) {
		return true;
	}
	if (auto pos = sym.name.rfind("::"); pos != std::string::npos) {
		std::string_view short_name(sym.name.data() + pos + 2, sym.name.size() - pos - 2);
		if (short_name == target_name) {
			return true;
		}
	}
	if (is_python) {
		if (auto pos = sym.name.rfind('.'); pos != std::string::npos) {
			std::string_view short_name(sym.name.data() + pos + 1, sym.name.size() - pos - 1);
			if (short_name == target_name) {
				return true;
			}
		}
	}
	std::string sep = is_python ? "." : "::";
	std::string suffix = sep + std::string(target_name);
	if (sym.name.size() >= suffix.size() &&
	    sym.name.compare(sym.name.size() - suffix.size(), suffix.size(), suffix) == 0) {
		return true;
	}
	return false;
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
	std::string map_err;
	if (auto mf = mapped_file::get_cached(path, map_err, /*populate=*/true)) {
		return mf->lines().size();
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
	if (args_.safe_path.empty()) {
		out_error = "Error: File path is empty.";
		return false;
	}
	return true;
}

std::string fs_read_symbol_tool::execute(agentlib::tool_context &ctx)
{
	if (args_.safe_path.find("://") == std::string::npos) {
		std::string map_err;
		auto mf = mapped_file::get_cached(args_.safe_path, map_err, /*populate=*/true);
		if (mf && mf->is_binary(/*treat_null_as_binary=*/true)) {
			return "Error: Cannot read symbols from binary file: " + args_.requested_path;
		}
	}

	ctx.file_drift_tracker.erase(args_.safe_path);

	// 1. Query document symbols via codemap_utils (uses LSP when available and codemap_fallbacks otherwise)
	std::vector<codemap_symbol_info> doc_symbols = get_document_codemap_symbols(args_.safe_path, ctx, 1);
	if (doc_symbols.empty() && project_manager::get_instance().lsp_is_supported_file(args_.safe_path)) {
		for (int attempt = 0; attempt < 5; ++attempt) {
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
			doc_symbols = get_document_codemap_symbols(args_.safe_path, ctx, 1);
			if (!doc_symbols.empty()) {
				break;
			}
		}
	}

	if (doc_symbols.empty()) {
		return "Error: No symbols returned by LSP. Either clangd is still initializing, or there are no functions/classes defined in this file.";
	}

	// 2. Find all matching symbols
	bool is_py = mime::get_language_from_extension(args_.safe_path) == "python";
	std::vector<codemap_symbol_info> matches;
	for (const auto &sym : doc_symbols) {
		if (sym.kind_str == "Class/Struct") {
			continue;
		}
		if (matches_codemap_symbol(sym, args_.symbol_name, is_py)) {
			matches.push_back(sym);
		}
	}
	if (matches.empty()) {
		for (const auto &sym : doc_symbols) {
			if (sym.kind_str == "Class/Struct" && matches_codemap_symbol(sym, args_.symbol_name, is_py)) {
				matches.push_back(sym);
			}
		}
	}

	// For Verilog/SystemVerilog files, also match <name>_mod or filename stem -> Module
	if (matches.empty()) {
		std::string ext = utf8::to_lower(std::filesystem::path(args_.safe_path).extension().string());
		if (ext == ".sv" || ext == ".svh" || ext == ".v" || ext == ".vh") {
			std::string stem = std::filesystem::path(args_.safe_path).stem().string();
			for (const auto &sym : doc_symbols) {
				if (sym.name == args_.symbol_name + "_mod" ||
				    args_.symbol_name == sym.name + "_mod" ||
				    (sym.kind_str == "Module" && args_.symbol_name == stem)) {
					matches.push_back(sym);
				}
			}
		}
	}

	if (matches.empty()) {
		return "Error: Symbol '" + args_.symbol_name + "' was not found in '" + args_.requested_path + "'.";
	}

	// 3. Read lines for each match (capped at max 20 matches)
	std::stringstream ss;
	size_t total_lines = count_total_lines(args_.safe_path, ctx);
	size_t num_matches = std::min<size_t>(matches.size(), 20);

	for (size_t i = 0; i < num_matches; ++i) {
		const auto &match = matches[i];
		int match_start = match.start_line;
		int match_end = match.end_line;

		int expanded_start = std::max(1, match_start - 2);
		int expanded_end = std::min(static_cast<int>(total_lines), match_end + 2);
		auto raw_lines = read_lines(args_.safe_path, expanded_start, expanded_end, ctx);

		int offset = match_start - expanded_start;
		int symbol_len = match_end - match_start;

		int keep_prev = 0;
		if (offset >= 1 && offset <= static_cast<int>(raw_lines.size()) && !utf8::is_whitespace_only(raw_lines[offset - 1])) {
			keep_prev = 1;
			if (offset >= 2 && !utf8::is_whitespace_only(raw_lines[offset - 2])) {
				keep_prev = 2;
			}
		}

		int symbol_end_idx = offset + symbol_len;
		int keep_next = 0;
		if (static_cast<int>(raw_lines.size()) > symbol_end_idx + 1 && symbol_end_idx + 1 >= 0 && !utf8::is_whitespace_only(raw_lines[symbol_end_idx + 1])) {
			keep_next = 1;
			if (static_cast<int>(raw_lines.size()) > symbol_end_idx + 2 && symbol_end_idx + 2 >= 0 && !utf8::is_whitespace_only(raw_lines[symbol_end_idx + 2])) {
				keep_next = 2;
			}
		}

		int start = match_start - keep_prev;
		int end = match_end + keep_next;

		// Safely slice target lines from raw_lines with explicit bounds clamping
		size_t slice_start = std::clamp<size_t>(offset - keep_prev >= 0 ? offset - keep_prev : 0, 0, raw_lines.size());
		size_t slice_end = std::clamp<size_t>(symbol_end_idx + keep_next + 1 >= 0 ? symbol_end_idx + keep_next + 1 : 0, slice_start, raw_lines.size());
		std::vector<std::string> lines(raw_lines.begin() + slice_start, raw_lines.begin() + slice_end);

		size_t max_backticks = count_max_consecutive_backticks(lines);
		size_t fence_len = std::max<size_t>(3, max_backticks + 1);
		std::string fence(fence_len, '`');
		std::string lang = mime::get_language_from_extension(args_.requested_path);

		if (matches.size() > 1) {
			ss << "### Match " << (i + 1) << ": Symbol '" << match.name << "'\n";
		}
		ss << "StartLine: " << match_start << "\n"
		   << "EndLine: " << match_end << "\n"
		   << "Code for lines " << start << " - " << end << " of " << args_.requested_path
		   << " (total " << total_lines << " lines):\n"
		   << fence << lang << "\n";
		int current_line = start;
		for (const auto &line : lines) {
			std::string clean_line;
			clean_line.reserve(line.size());
			for (unsigned char c : line) {
				if (c < 32 && c != '\t') continue;
				if (c == 127) continue;
				clean_line.push_back(static_cast<char>(c));
			}
			ss << current_line << ": " << clean_line << "\n";
			current_line++;
		}
		ss << fence << "\n\n";
	}

	return fs_utils::wrap_prompt_untrusted_data_tag("fs_read_symbol_result", ss.str());
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

	std::string map_err;
	if (auto mf = mapped_file::get_cached(path, map_err, /*populate=*/true)) {
		auto all_lines = mf->lines();
		if (end >= start && start >= 1 && static_cast<size_t>(start) <= all_lines.size()) {
			size_t first_idx = static_cast<size_t>(start - 1);
			size_t last_idx = std::min<size_t>(static_cast<size_t>(end), all_lines.size());
			lines.reserve(last_idx - first_idx);
			for (size_t i = first_idx; i < last_idx; ++i) {
				lines.emplace_back(all_lines[i]);
			}
		}
	}
	return lines;
}

} // namespace tools
