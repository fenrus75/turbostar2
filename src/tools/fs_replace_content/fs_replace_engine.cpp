#include "fs_replace_engine.h"

#include "agentlib/file_health_utils.h"
#include "agentlib/virtual_file_system.h"
#include "codemap_utils.h"
#include "mime.h"
#include "project_manager.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <format>
#include <sstream>
#include <unistd.h>

namespace tools {

static std::vector<std::string> split_lines(const std::string &str)
{
	std::vector<std::string> res;
	std::stringstream ss(str);
	std::string line;
	while (std::getline(ss, line)) {
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		res.push_back(line);
	}
	return res;
}

static std::string normalize_line_for_relaxed(std::string_view line, bool strip_leading)
{
	while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
		line.remove_suffix(1);
	}
	if (strip_leading) {
		while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
			line.remove_prefix(1);
		}
		return std::string(line);
	}
	std::string res;
	res.reserve(line.size() * 2);
	for (char c : line) {
		if (c == '\t') {
			res.append("    ");
		} else {
			res.push_back(c);
		}
	}
	return res;
}

static void get_line_byte_range(const std::string &text, int start_line_1based, int num_lines, bool target_ends_with_newline, size_t &out_start, size_t &out_len)
{
	int current_line = 1;
	size_t idx = 0;
	size_t n = text.size();
	while (idx < n && current_line < start_line_1based) {
		if (text[idx] == '\n') {
			current_line++;
		}
		idx++;
	}
	out_start = idx;
	int lines_counted = 0;
	while (idx < n && lines_counted < num_lines) {
		if (text[idx] == '\n') {
			lines_counted++;
			if (lines_counted == num_lines && !target_ends_with_newline) {
				break;
			}
		}
		idx++;
	}
	out_len = idx - out_start;
}

int fs_replace_engine::calculate_brace_balance(const std::vector<std::string> &lines, int start_line_0, int end_line_0)
{
	int balance = 0;
	bool in_block_comment = false;
	bool in_string = false;
	char string_quote = '\0';

	for (int i = start_line_0; i <= end_line_0 && i < static_cast<int>(lines.size()); ++i) {
		const std::string &line = lines[i];
		size_t len = line.length();
		size_t j = 0;

		while (j < len) {
			if (in_block_comment) {
				if (j + 1 < len && line[j] == '*' && line[j + 1] == '/') {
					in_block_comment = false;
					j += 2;
				} else {
					j++;
				}
				continue;
			}

			if (in_string) {
				if (line[j] == '\\' && j + 1 < len) {
					j += 2;
				} else if (line[j] == string_quote) {
					in_string = false;
					j++;
				} else {
					j++;
				}
				continue;
			}

			if (j + 1 < len && line[j] == '/' && line[j + 1] == '/') {
				break;
			}
			if (j + 1 < len && line[j] == '/' && line[j + 1] == '*') {
				in_block_comment = true;
				j += 2;
				continue;
			}

			if (line[j] == '"' || line[j] == '\'') {
				in_string = true;
				string_quote = line[j];
				j++;
				continue;
			}

			if (line[j] == '{') {
				balance++;
			} else if (line[j] == '}') {
				balance--;
			}
			j++;
		}
	}
	return balance;
}

std::string fs_replace_engine::check_brace_warnings(
	const std::string &safe_path,
	agentlib::tool_context &ctx,
	const std::vector<std::string> &before_lines,
	const std::vector<std::string> &after_lines,
	const std::vector<std::pair<int, int>> &edited_ranges_0based,
	int net_line_delta)
{
	bool file_has_braces = false;
	for (const auto &l : before_lines) {
		if (l.find('{') != std::string::npos || l.find('}') != std::string::npos) {
			file_has_braces = true;
			break;
		}
	}
	if (!mime::uses_brace_syntax(safe_path) && !file_has_braces) {
		return "";
	}

	std::string warnings;

	// 1. Check Whole-File Brace Balance
	if (!before_lines.empty() && !after_lines.empty()) {
		int before_whole = calculate_brace_balance(before_lines, 0, static_cast<int>(before_lines.size()) - 1);
		int after_whole = calculate_brace_balance(after_lines, 0, static_cast<int>(after_lines.size()) - 1);

		// If the file was balanced at file scope and the edit made it unbalanced at file scope:
		if (before_whole == 0 && after_whole != 0) {
			if (after_whole > 0) {
				warnings += std::format("⚠️ Warning: Edit introduced unbalanced braces at file scope (net balance: +{}, missing {} closing '}}' brace{})\n",
						        after_whole, after_whole, (after_whole == 1 ? "" : "s"));
			} else {
				int abs_bal = std::abs(after_whole);
				warnings += std::format("⚠️ Warning: Edit introduced unbalanced braces at file scope (net balance: {}, possible extra {} closing '}}' brace{})\n",
						        after_whole, abs_bal, (abs_bal == 1 ? "" : "s"));
			}
			return warnings;
		}
	}

	// 2. If global counter did NOT transition 0 -> non-0 (e.g. was and remains unbalanced, or remains 0->0):
	// THEN look at function/symbol scope counter.
	auto symbols = get_document_codemap_symbols(safe_path, ctx);
	if (!symbols.empty()) {
		for (const auto &sym : symbols) {
			if (sym.kind_str != "Function" && sym.kind_str != "Method" &&
			    sym.kind_str != "Class" && sym.kind_str != "Struct" && sym.kind_str != "Interface") {
				continue;
			}

			int f_start = sym.start_line - 1;
			int f_end = sym.end_line - 1;
			if (f_start < 0 || f_end >= static_cast<int>(before_lines.size()) || f_start > f_end) {
				continue;
			}

			bool intersects_edit = false;
			for (const auto &range : edited_ranges_0based) {
				if (!(range.second < f_start || range.first > f_end)) {
					intersects_edit = true;
					break;
				}
			}

			if (intersects_edit) {
				int before_sym_bal = calculate_brace_balance(before_lines, f_start, f_end);
				int after_f_end = std::min(static_cast<int>(after_lines.size()) - 1, f_end + net_line_delta);
				if (after_f_end >= f_start) {
					int after_sym_bal = calculate_brace_balance(after_lines, f_start, after_f_end);
					if (before_sym_bal == 0 && after_sym_bal != 0) {
						if (after_sym_bal > 0) {
							warnings += std::format("⚠️ Warning: Edit introduced unbalanced braces in {} '{}' (net balance: +{}, missing {} closing '}}' brace{})\n",
										sym.kind_str, sym.name, after_sym_bal, after_sym_bal, (after_sym_bal == 1 ? "" : "s"));
						} else {
							int abs_bal = std::abs(after_sym_bal);
							warnings += std::format("⚠️ Warning: Edit introduced unbalanced braces in {} '{}' (net balance: {}, possible extra {} closing '}}' brace{})\n",
										sym.kind_str, sym.name, after_sym_bal, abs_bal, (abs_bal == 1 ? "" : "s"));
						}
					}
				}
			}
		}
	}

	return warnings;

}

replace_engine_result fs_replace_engine::execute(agentlib::tool_context &ctx, const replace_engine_args &args)
{
	replace_engine_result res;

	std::string path_to_use = args.safe_path;
	auto *vfs = ctx.fs_security.get_vfs();
	if (vfs && vfs->is_local_path_available(args.safe_path)) {
		path_to_use = vfs->get_local_path(args.safe_path);
	}

	if (!std::filesystem::exists(path_to_use)) {
		res.error_message = "Error: File does not exist. Replacement can only edit existing files.";
		return res;
	}

	std::string file_content;
	{
		std::ifstream in(path_to_use, std::ios::binary);
		if (!in.is_open()) {
			res.error_message = "Error: Could not open file for reading.";
			return res;
		}
		file_content.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	}

	std::vector<std::string> current_lines = split_lines(file_content);
	res.before_lines = current_lines;

	std::vector<std::pair<int, int>> edited_ranges_0based;
	int cumulative_line_delta = 0;
	std::string work_content = file_content;

	for (size_t chunk_idx = 0; chunk_idx < args.chunks.size(); ++chunk_idx) {
		const auto &chunk = args.chunks[chunk_idx];

		if (chunk.target_content.empty()) {
			res.error_message = std::format("Error: Chunk {} has empty target_content.", chunk_idx + 1);
			return res;
		}

		int scope_start_line = 1;
		int scope_end_line = static_cast<int>(current_lines.size());

		// 1. Resolve function scope via codemap_utils
		if (!chunk.function_scope.empty()) {
			auto symbols = get_document_codemap_symbols(args.safe_path, ctx);
			const auto *sym = find_symbol_by_hint(symbols, chunk.function_scope);
			if (sym) {
				scope_start_line = sym->start_line;
				scope_end_line = sym->end_line;
			}
		} else if (chunk.start_line > 0 || chunk.end_line > 0) {
			if (chunk.start_line > 0) scope_start_line = chunk.start_line;
			if (chunk.end_line > 0) scope_end_line = chunk.end_line;
		}

		scope_start_line = std::clamp(scope_start_line, 1, static_cast<int>(current_lines.size()));
		scope_end_line = std::clamp(scope_end_line, scope_start_line, static_cast<int>(current_lines.size()));

		// 2. Perform search in work_content
		size_t match_pos = std::string::npos;
		size_t match_len = 0;
		int matched_start_line_1based = -1;

		std::vector<std::string> target_lines = split_lines(chunk.target_content);
		int target_line_count = std::max(1, static_cast<int>(target_lines.size()));
		bool target_ends_with_newline = chunk.target_content.ends_with('\n');

		// Scope search window check
		size_t scope_byte_start = 0;
		size_t scope_byte_len = work_content.size();
		get_line_byte_range(work_content, scope_start_line, scope_end_line - scope_start_line + 1, true, scope_byte_start, scope_byte_len);

		// Collect all occurrences of target_content in search scope
		std::vector<std::pair<size_t, int>> exact_occurrences;
		size_t search_pos = scope_byte_start;
		size_t scope_end_byte = scope_byte_start + scope_byte_len;
		while (search_pos < scope_end_byte) {
			size_t found = work_content.find(chunk.target_content, search_pos);
			if (found == std::string::npos || found >= scope_end_byte) {
				break;
			}
			int line_num = 1;
			for (size_t idx = 0; idx < found; ++idx) {
				if (work_content[idx] == '\n') line_num++;
			}
			exact_occurrences.push_back({found, line_num});
			search_pos = found + std::max<size_t>(1, chunk.target_content.size());
		}

		if (exact_occurrences.size() > 1) {
			if (chunk.line_hint > 0) {
				// Select occurrence closest to line_hint
				size_t best_idx = 0;
				int min_dist = std::abs(exact_occurrences[0].second - chunk.line_hint);
				for (size_t k = 1; k < exact_occurrences.size(); ++k) {
					int dist = std::abs(exact_occurrences[k].second - chunk.line_hint);
					if (dist < min_dist) {
						min_dist = dist;
						best_idx = k;
					}
				}
				match_pos = exact_occurrences[best_idx].first;
				match_len = chunk.target_content.size();
				matched_start_line_1based = exact_occurrences[best_idx].second;
			} else {
				std::stringstream err_ss;
				err_ss << "Error: Multiple matches (" << exact_occurrences.size() << ") found for target_content in " << args.path << " at line numbers: [";
				for (size_t k = 0; k < exact_occurrences.size(); ++k) {
					err_ss << exact_occurrences[k].second << (k + 1 < exact_occurrences.size() ? ", " : "");
				}
				err_ss << "]. ";
				if (!chunk.function_scope.empty()) {
					err_ss << "Multiple occurrences exist even within function/scope '" << chunk.function_scope << "'. Please pass 'line_hint' to specify which line to edit, or include more context lines in target_content.";
				} else {
					err_ss << "Please pass 'function_hint' parameter with the enclosing function/method name (e.g. 'execute_disk_fallback'), or pass 'line_hint' to specify which occurrence to edit.";
				}
				res.error_message = err_ss.str();
				return res;
			}
		} else if (exact_occurrences.size() == 1) {
			match_pos = exact_occurrences[0].first;
			match_len = chunk.target_content.size();
			matched_start_line_1based = exact_occurrences[0].second;
		}

		// Relaxed line matching fallback if exact match failed
		if (match_pos == std::string::npos && !target_lines.empty()) {
			std::vector<std::string> norm_target_strip;
			for (const auto &tl : target_lines) {
				norm_target_strip.push_back(normalize_line_for_relaxed(tl, true));
			}

			int max_search_start = static_cast<int>(current_lines.size()) - target_line_count;
			for (int start_idx = scope_start_line - 1; start_idx <= scope_end_line - 1 && start_idx <= max_search_start; ++start_idx) {
				bool match = true;
				for (int k = 0; k < target_line_count; ++k) {
					std::string curr_norm = normalize_line_for_relaxed(current_lines[start_idx + k], true);
					if (curr_norm != norm_target_strip[k]) {
						match = false;
						break;
					}
				}
				if (match) {
					matched_start_line_1based = start_idx + 1;
					get_line_byte_range(work_content, matched_start_line_1based, target_line_count, target_ends_with_newline, match_pos, match_len);
					break;
				}
			}

			if (match_pos == std::string::npos) {
				// Whole file relaxed search
				for (int start_idx = 0; start_idx <= max_search_start; ++start_idx) {
					bool match = true;
					for (int k = 0; k < target_line_count; ++k) {
						std::string curr_norm = normalize_line_for_relaxed(current_lines[start_idx + k], true);
						if (curr_norm != norm_target_strip[k]) {
							match = false;
							break;
						}
					}
					if (match) {
						matched_start_line_1based = start_idx + 1;
						get_line_byte_range(work_content, matched_start_line_1based, target_line_count, target_ends_with_newline, match_pos, match_len);
						break;
					}
				}
			}

			if (matched_start_line_1based >= 1 && matched_start_line_1based <= static_cast<int>(current_lines.size()) && !target_lines.empty()) {
				size_t file_indent_len = 0;
				const std::string &file_line = current_lines[matched_start_line_1based - 1];
				while (file_indent_len < file_line.size() && (file_line[file_indent_len] == ' ' || file_line[file_indent_len] == '\t')) {
					file_indent_len++;
				}
				size_t replacement_indent_len = 0;
				while (replacement_indent_len < chunk.replacement_content.size() && (chunk.replacement_content[replacement_indent_len] == ' ' || chunk.replacement_content[replacement_indent_len] == '\t')) {
					replacement_indent_len++;
				}
				if (file_indent_len > 0 && replacement_indent_len == 0) {
					if (match_len >= file_indent_len) {
						match_pos += file_indent_len;
						match_len -= file_indent_len;
					}
				}
			}
		}

		if (match_pos == std::string::npos) {
			if (args.chunks.size() == 1) {
				res.error_message = std::format("Error: target_content not found in {}.", args.path);
			} else {
				res.error_message = std::format("Error: Could not locate target_content for chunk {} in {}.", chunk_idx + 1, args.path);
			}
			return res;
		}

		if (match_pos != std::string::npos && !chunk.replacement_content.empty()) {
			// Find start of line for match_pos
			size_t line_start_pos = match_pos;
			while (line_start_pos > 0 && work_content[line_start_pos - 1] != '\n') {
				line_start_pos--;
			}
			bool preceded_by_only_ws = true;
			for (size_t k = line_start_pos; k < match_pos; ++k) {
				if (work_content[k] != ' ' && work_content[k] != '\t') {
					preceded_by_only_ws = false;
					break;
				}
			}
			bool replacement_starts_with_ws = (chunk.replacement_content[0] == ' ' || chunk.replacement_content[0] == '\t');
			if (preceded_by_only_ws && replacement_starts_with_ws && line_start_pos < match_pos) {
				match_len += (match_pos - line_start_pos);
				match_pos = line_start_pos;
			}
		}

		// Apply substitution to work_content
		work_content.replace(match_pos, match_len, chunk.replacement_content);

		int replacement_line_count = 1;
		for (char c : chunk.replacement_content) {
			if (c == '\n') replacement_line_count++;
		}
		int line_delta = replacement_line_count - target_line_count;
		cumulative_line_delta += line_delta;

		int edit_start_0 = matched_start_line_1based - 1;
		int edit_end_0 = edit_start_0 + target_line_count - 1;
		edited_ranges_0based.push_back({edit_start_0, edit_end_0});

		current_lines = split_lines(work_content);
		res.chunks_applied++;
	}

	res.after_lines = current_lines;

	// 3. Brace balance check
	std::string brace_warnings = check_brace_warnings(
		args.safe_path, ctx, res.before_lines, res.after_lines, edited_ranges_0based, cumulative_line_delta);

	if (args.strict && !brace_warnings.empty()) {
		res.error_message = "Error: Edit rejected (strict mode): it would leave unbalanced braces and was not applied.\n" + brace_warnings;
		return res;
	}

	// 4. Write back to disk via atomic temp file + rename
	std::string tmp_path = path_to_use + ".tmp." + std::to_string(getpid());
	{
		std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
		if (!out.is_open()) {
			res.error_message = "Error: Could not open temporary file for writing during execution.";
			return res;
		}
		out.write(work_content.data(), work_content.length());
		out.close();
	}

	std::error_code ec_rename;
	std::filesystem::rename(tmp_path, path_to_use, ec_rename);
	if (ec_rename) {
		std::filesystem::remove(tmp_path, ec_rename);
		std::ofstream out(path_to_use, std::ios::binary | std::ios::trunc);
		if (!out.is_open()) {
			res.error_message = "Error: Could not open target file for writing during execution.";
			return res;
		}
		out.write(work_content.data(), work_content.length());
		out.close();
	}

	std::string edit_id = agentlib::update_file_health_state(ctx, args.safe_path);
	if (res.chunks_applied == 1 && !edited_ranges_0based.empty()) {
		res.result_text = std::format("Successfully replaced target_content in {} starting at line {} [Edit ID: {}].",
					      args.path, edited_ranges_0based[0].first + 1, edit_id);
	} else {
		res.result_text = std::format("Successfully applied {} chunk replacements in {} [Edit ID: {}].",
					      res.chunks_applied, args.path, edit_id);
	}

	if (!brace_warnings.empty()) {
		res.result_text += "\n" + brace_warnings;
		res.warning_message = brace_warnings;
	}

	res.success = true;
	return res;
}

} // namespace tools
