#include "languages/cpp/symbol_extractor.h"

#include <algorithm>
#include <re2/re2.h>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>
#include "languages/cpp/source_masker.h"
#include "project_manager.h"
#include "utf8.h"

namespace languages::cpp
{

bool symbol_extractor::is_function_specifier_word(std::string_view word)
{
	static const std::unordered_set<std::string_view> keywords = {
	    "static",	"inline",  "virtual",	 "explicit", "extern",	"constexpr", "consteval", "noexcept", "noinstr", "__always_inline",
	    "__init",	"__exit",  "asmlinkage", "__sched",  "void",	"int",	     "bool",	  "char",     "long",	 "short",
	    "unsigned", "signed",  "size_t",	 "ssize_t",  "uint8_t", "uint16_t",  "uint32_t",  "uint64_t", "int8_t",	 "int16_t",
	    "int32_t",	"int64_t", "u8",	 "u16",	     "u32",	"u64",	     "s8",	  "s16",      "s32",	 "s64",
	    "ktime_t",	"auto",	   "double",	 "float",    "const"};
	return keywords.contains(word);
}

void symbol_extractor::extract_symbols(std::span<const std::string> lines, int min_lines, std::vector<tools::codemap_symbol_info> &out)
{
	static const re2::RE2 cpp_func_head_regex(R"(^(\s*(?:[\w:<>]+[\s*&]*\s+)*[\s*&]*([a-zA-Z_]\w*(?:::[a-zA-Z_]\w*)*)\s*\())");
	static const re2::RE2 cpp_class_regex(
	    R"(^(\s*(?:typedef\s+)?(?:class|struct)\s+(?:\[\[.*?\]\]\s+)?(?:__attribute__\s*\(\(.*?\)\)\s+)?([a-zA-Z_]\w*)))");
	static const re2::RE2 cpp_enum_regex(R"(^\s*(?:typedef\s+)?enum(?:\s+class|\s+struct)?\s+([a-zA-Z_]\w*))");

	std::vector<std::string> masked_lines = source_masker::mask_lines(lines);

	for (size_t i = 0; i < masked_lines.size(); ++i) {
		if ((i & 63) == 0 && project_manager::get_instance().is_exiting()) {
			return;
		}
		int line_num = static_cast<int>(i + 1);

		bool is_class_candidate = false;
		re2::StringPiece full_class_match, class_name_sp;
		if (re2::RE2::PartialMatch(masked_lines[i], cpp_class_regex, &full_class_match, &class_name_sp)) {
			is_class_candidate = true;
			// Filter out parameter usages such as 'struct cpuidle_device *dev, int index)'
			std::string_view line_v = masked_lines[i];
			size_t match_end = static_cast<size_t>(full_class_match.data() - line_v.data()) + full_class_match.size();
			std::string_view suffix = (match_end <= line_v.size()) ? line_v.substr(match_end) : std::string_view{};
			size_t first_non_ws = suffix.find_first_not_of(" \t\r\n");
			if (first_non_ws != std::string_view::npos) {
				char next_ch = suffix[first_non_ws];
				if (next_ch == '*' || next_ch == '&' || next_ch == ',' || next_ch == ')') {
					is_class_candidate = false;
				}
			}
			size_t close_paren = suffix.find(')');
			size_t open_brace = suffix.find('{');
			if (close_paren != std::string_view::npos && (open_brace == std::string_view::npos || close_paren < open_brace)) {
				is_class_candidate = false;
			}
		}

		re2::StringPiece full_func_match, func_name_sp;
		std::string enum_name;
		if (is_class_candidate) {
			std::string_view trimmed_line = masked_lines[i];
			size_t last_non_ws = trimmed_line.find_last_not_of(" \t\r\n");
			if (last_non_ws != std::string_view::npos && trimmed_line[last_non_ws] == ';') {
				continue;
			}

			int end_line = line_num;
			int depth = 0;
			bool started = false;
			for (size_t j = i; j < masked_lines.size(); ++j) {
				for (char c : masked_lines[j]) {
					if (c == '{') {
						depth++;
						started = true;
					} else if (c == '}') {
						depth--;
					}
				}
				if (started && depth == 0) {
					end_line = static_cast<int>(j + 1);
					break;
				}
			}
			if (started) {
				int len = end_line - line_num + 1;
				if (len >= min_lines) {
					std::string kind_val = (masked_lines[i].find("struct") != std::string::npos) ? "Struct" : "Class";
					std::string class_name(class_name_sp);
					out.push_back({class_name, class_name, kind_val, line_num, end_line, len, 0, ""});
				}
			}
		} else if (re2::RE2::PartialMatch(masked_lines[i], cpp_enum_regex, &enum_name)) {
			std::string_view trimmed_line = masked_lines[i];
			size_t last_non_ws = trimmed_line.find_last_not_of(" \t\r\n");
			if (last_non_ws != std::string_view::npos && trimmed_line[last_non_ws] == ';') {
				continue;
			}

			int end_line = line_num;
			int depth = 0;
			bool started = false;
			for (size_t j = i; j < masked_lines.size(); ++j) {
				for (char c : masked_lines[j]) {
					if (c == '{') {
						depth++;
						started = true;
					} else if (c == '}') {
						depth--;
					}
				}
				if (started && depth == 0) {
					end_line = static_cast<int>(j + 1);
					break;
				}
			}
			if (started) {
				int len = end_line - line_num + 1;
				if (len >= min_lines) {
					out.push_back({enum_name, enum_name, "Enum", line_num, end_line, len, 0, ""});
				}
			}
		} else if (re2::RE2::PartialMatch(masked_lines[i], cpp_func_head_regex, &full_func_match, &func_name_sp)) {
			std::string name(func_name_sp);
			if (name != "if" && name != "for" && name != "while" && name != "switch" && name != "catch" &&
			    name != "__attribute__" && !name.starts_with("__builtin_")) {
				// Forward scan to find body '{' and verify it's not a prototype ';'
				int paren_depth = 0;
				bool found_brace = false;
				size_t brace_line_idx = i;
				bool is_prototype = false;
				size_t func_head_pos = static_cast<size_t>(full_func_match.data() - masked_lines[i].data());

				for (size_t j = i; j < std::min(masked_lines.size(), i + 40); ++j) {
					std::string_view scan_line = masked_lines[j];
					size_t comment_pos = scan_line.find("//");
					if (comment_pos != std::string_view::npos) {
						scan_line = scan_line.substr(0, comment_pos);
					}

					for (size_t char_idx = (j == i ? func_head_pos : 0); char_idx < scan_line.size(); ++char_idx) {
						char c = scan_line[char_idx];
						if (c == '(') {
							paren_depth++;
						} else if (c == ')') {
							paren_depth--;
						} else if (c == ';' && paren_depth <= 0) {
							is_prototype = true;
							break;
						} else if (c == '{' && paren_depth <= 0) {
							found_brace = true;
							brace_line_idx = j;
							break;
						}
					}
					if (is_prototype || found_brace) {
						break;
					}
				}

				if (!found_brace || is_prototype) {
					continue;
				}

				// Find upper bound by searching backwards from line i for previous '}'
				int upper_bound_idx = -1;
				for (int k = static_cast<int>(i) - 1; k >= 0; --k) {
					std::string_view rev_line = masked_lines[k];
					size_t comment_pos = rev_line.find("//");
					if (comment_pos != std::string_view::npos) {
						rev_line = rev_line.substr(0, comment_pos);
					}
					if (rev_line.find('}') != std::string_view::npos) {
						upper_bound_idx = k;
						break;
					}
				}

				// Walk backwards from line i to find the true function start line
				int func_start = static_cast<int>(i + 1);
				for (int k = static_cast<int>(i) - 1; k > upper_bound_idx; --k) {
					std::string_view l_view = masked_lines[k];
					size_t c_pos = l_view.find("//");
					if (c_pos != std::string_view::npos) {
						l_view = l_view.substr(0, c_pos);
					}
					size_t first_non = l_view.find_first_not_of(" \t\r\n");
					if (first_non == std::string_view::npos) {
						// Blank line provides a bound on the start of the function
						break;
					}
					std::string_view trimmed = l_view.substr(first_non);
					if (trimmed.starts_with('#') || trimmed.starts_with("/*") || trimmed.ends_with("*/") ||
					    trimmed.find(';') != std::string_view::npos || trimmed.find('}') != std::string_view::npos) {
						break;
					}

					// Check if line contains function specifier keywords (static, void, inline, etc.)
					bool has_specifier = false;
					std::string token;
					for (size_t ci = 0; ci <= trimmed.size(); ++ci) {
						char c = (ci < trimmed.size()) ? trimmed[ci] : ' ';
						if (utf8::is_alnum(c, "_")) {
							token += c;
						} else {
							if (!token.empty()) {
								if (is_function_specifier_word(token)) {
									has_specifier = true;
									break;
								}
								token.clear();
							}
						}
					}
					if (has_specifier) {
						func_start = k + 1;
					} else {
						break;
					}
				}

				// Match braces starting from brace_line_idx to determine end_line
				int end_line = func_start;
				int depth = 0;
				bool started = false;
				for (size_t j = brace_line_idx; j < masked_lines.size(); ++j) {
					for (char c : masked_lines[j]) {
						if (c == '{') {
							depth++;
							started = true;
						} else if (c == '}') {
							depth--;
						}
					}
					if (started && depth == 0) {
						end_line = static_cast<int>(j + 1);
						break;
					}
				}

				if (started) {
					int len = std::max(1, end_line - func_start + 1);
					if (len >= min_lines) {
						out.push_back({name, name, "Function", func_start, end_line, len, 0, ""});
					}
				}
			}
		}
	}
}

} // namespace languages::cpp
