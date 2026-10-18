#include "codemap_fallbacks.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>
#include <re2/re2.h>
#include "fs_utils.h"
#include "project_manager.h"
#include "utf8.h"

namespace tools
{

bool is_markdown_file(const std::string_view path) noexcept
{
	std::string ext = utf8::to_lower(std::filesystem::path(path).extension().string());
	return ext == ".md" || ext == ".markdown" || ext == ".mdown" || ext == ".mkd";
}

void parse_markdown_headings(const std::string &content, int min_lines, std::vector<codemap_symbol_info> &out)
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
		// Trim trailing whitespace (spaces, tabs, newlines, CRLF, and Unicode whitespace).
		std::string heading = utf8::trim_trailing_whitespace(line_text.substr(text_start));
		// Strip a closing ATX hash sequence ("## foo #####"), which CommonMark allows:
		// any run of trailing '#'s, as long as it's preceded by whitespace.
		if (heading.ends_with('#')) {
			size_t end_hashes = 0;
			while (end_hashes < heading.size() && heading[heading.size() - 1 - end_hashes] == '#') {
				++end_hashes;
			}
			// The hash run must be preceded by whitespace (or the very start) to count
			// as a closing sequence.
			if (end_hashes < heading.size() &&
			    (heading[heading.size() - 1 - end_hashes] == ' ' || heading[heading.size() - 1 - end_hashes] == '\t')) {
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

static bool is_cpp_function_specifier_word(std::string_view word)
{
	static const std::unordered_set<std::string_view> keywords = {
	    "static",	"inline",  "virtual",	 "explicit", "extern",	"constexpr", "consteval", "noexcept", "noinstr", "__always_inline",
	    "__init",	"__exit",  "asmlinkage", "__sched",  "void",	"int",	     "bool",	  "char",     "long",	 "short",
	    "unsigned", "signed",  "size_t",	 "ssize_t",  "uint8_t", "uint16_t",  "uint32_t",  "uint64_t", "int8_t",	 "int16_t",
	    "int32_t",	"int64_t", "u8",	 "u16",	     "u32",	"u64",	     "s8",	  "s16",      "s32",	 "s64",
	    "ktime_t",	"auto",	   "double",	 "float",    "const"};
	return keywords.contains(word);
}

void fallback_find_symbols(const std::string &safe_path, int min_lines, std::vector<codemap_symbol_info> &out)
{
	std::string abs_path = fs_utils::safe_absolute(safe_path).string();
	std::ifstream in(abs_path);
	if (!in.is_open()) {
		in.open(safe_path);
		if (!in.is_open())
			return;
	}

	std::vector<std::string> lines;
	std::string l;
	while (std::getline(in, l)) {
		lines.push_back(l);
	}

	std::string ext = utf8::to_lower(std::filesystem::path(safe_path).extension().string());
	bool is_py = (ext == ".py");
	bool is_sv = (ext == ".sv" || ext == ".svh" || ext == ".v" || ext == ".vh");

	static const re2::RE2 cpp_func_head_regex(R"(^(\s*(?:[\w:<>]+[\s*&]*\s+)*[\s*&]*([a-zA-Z_]\w*(?:::[a-zA-Z_]\w*)*)\s*\())");
	static const re2::RE2 cpp_class_regex(
	    R"(^(\s*(?:typedef\s+)?(?:class|struct)\s+(?:\[\[.*?\]\]\s+)?(?:__attribute__\s*\(\(.*?\)\)\s+)?([a-zA-Z_]\w*)))");
	static const re2::RE2 cpp_enum_regex(R"(^\s*(?:typedef\s+)?enum(?:\s+class|\s+struct)?\s+([a-zA-Z_]\w*))");
	static const re2::RE2 py_func_regex(R"(^\s*def\s+([a-zA-Z_]\w*)\s*\()");
	static const re2::RE2 py_class_regex(R"(^\s*class\s+([a-zA-Z_]\w*))");

	static const re2::RE2 sv_module_regex(R"(^\s*(?:extern\s+)?module\s+([a-zA-Z_]\w*))");
	static const re2::RE2 sv_interface_regex(R"(^\s*(?:extern\s+)?interface\s+([a-zA-Z_]\w*))");
	static const re2::RE2 sv_package_regex(R"(^\s*package\s+([a-zA-Z_]\w*))");
	static const re2::RE2 sv_class_regex(R"(^\s*(?:virtual\s+)?class\s+([a-zA-Z_]\w*))");
	static const re2::RE2 sv_func_regex(
	    R"(^\s*(?:(?:pure\s+virtual|virtual|static|local|protected)\s+)*function\s+(?:.*?\s+)?([a-zA-Z_]\w*)\s*[\(;])");
	static const re2::RE2 sv_task_regex(
	    R"(^\s*(?:(?:pure\s+virtual|virtual|static|local|protected)\s+)*task\s+(?:.*?\s+)?([a-zA-Z_]\w*)\s*[\(;])");

	for (size_t i = 0; i < lines.size(); ++i) {
		if ((i & 63) == 0 && project_manager::get_instance().is_exiting()) {
			return;
		}
		int line_num = static_cast<int>(i + 1);

		if (is_py) {
			std::string name;
			if (re2::RE2::PartialMatch(lines[i], py_class_regex, &name) ||
			    re2::RE2::PartialMatch(lines[i], py_func_regex, &name)) {
				out.push_back({name, name, "Function", line_num, line_num, 1, 0, ""});
			}
			continue;
		}

		if (is_sv) {
			std::string name;
			std::string kind;
			std::string end_kw;

			if (re2::RE2::PartialMatch(lines[i], sv_module_regex, &name)) {
				kind = "Module";
				end_kw = "endmodule";
			} else if (re2::RE2::PartialMatch(lines[i], sv_interface_regex, &name)) {
				kind = "Interface";
				end_kw = "endinterface";
			} else if (re2::RE2::PartialMatch(lines[i], sv_package_regex, &name)) {
				kind = "Package";
				end_kw = "endpackage";
			} else if (re2::RE2::PartialMatch(lines[i], sv_class_regex, &name)) {
				kind = "Class";
				end_kw = "endclass";
			} else if (re2::RE2::PartialMatch(lines[i], sv_func_regex, &name)) {
				kind = "Function";
				end_kw = "endfunction";
			} else if (re2::RE2::PartialMatch(lines[i], sv_task_regex, &name)) {
				kind = "Task";
				end_kw = "endtask";
			}

			if (!name.empty()) {
				int end_line = line_num;
				for (size_t j = i + 1; j < lines.size(); ++j) {
					if (lines[j].find(end_kw) != std::string::npos) {
						end_line = static_cast<int>(j + 1);
						break;
					}
				}
				int len = end_line - line_num + 1;
				if (len >= min_lines) {
					out.push_back({name, name, kind, line_num, end_line, len, 0, ""});
				}
			}
			continue;
		}

		bool is_class_candidate = false;
		re2::StringPiece full_class_match, class_name_sp;
		if (re2::RE2::PartialMatch(lines[i], cpp_class_regex, &full_class_match, &class_name_sp)) {
			is_class_candidate = true;
			// Filter out parameter usages such as 'struct cpuidle_device *dev, int index)'
			std::string_view line_v = lines[i];
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
					std::string kind_val = (lines[i].find("struct") != std::string::npos) ? "Struct" : "Class";
					std::string class_name(class_name_sp);
					out.push_back({class_name, class_name, kind_val, line_num, end_line, len, 0, ""});
				}
			}
		} else if (re2::RE2::PartialMatch(lines[i], cpp_enum_regex, &enum_name)) {
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
		} else if (re2::RE2::PartialMatch(lines[i], cpp_func_head_regex, &full_func_match, &func_name_sp)) {
			std::string name(func_name_sp);
			if (name != "if" && name != "for" && name != "while" && name != "switch" && name != "catch" &&
			    name != "__attribute__" && !name.starts_with("__builtin_")) {
				// Forward scan to find body '{' and verify it's not a prototype ';'
				int paren_depth = 0;
				bool found_brace = false;
				size_t brace_line_idx = i;
				bool is_prototype = false;
				size_t func_head_pos = static_cast<size_t>(full_func_match.data() - lines[i].data());

				for (size_t j = i; j < std::min(lines.size(), i + 40); ++j) {
					std::string_view scan_line = lines[j];
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
					std::string_view rev_line = lines[k];
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
					std::string_view l_view = lines[k];
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
								if (is_cpp_function_specifier_word(token)) {
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
				for (size_t j = brace_line_idx; j < lines.size(); ++j) {
					for (char c : lines[j]) {
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

} // namespace tools
