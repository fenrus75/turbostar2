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
#include "languages/cpp/symbol_extractor.h"
#include "project_manager.h"
#include "utf8.h"

namespace tools
{

bool is_markdown_file(const std::string_view path) noexcept
{
	std::string ext = utf8::to_lower(std::filesystem::path(path).extension().string());
	return ext == ".md" || ext == ".markdown" || ext == ".mdown" || ext == ".mkd";
}

void fallback_find_symbols_markdown(std::span<const std::string> lines, int min_lines, std::vector<codemap_symbol_info> &out)
{
	std::vector<codemap_symbol_info> headings;
	std::vector<int> stack; // heading levels forming the current ancestor chain

	for (size_t i = 0; i < lines.size(); ++i) {
		int line_no = static_cast<int>(i + 1);
		const std::string &line_text = lines[i];
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

void fallback_find_symbols_markdown(std::span<const std::string> lines, std::vector<codemap_symbol_info> &out)
{
	fallback_find_symbols_markdown(lines, 1, out);
}

void parse_markdown_headings(const std::string &content, int min_lines, std::vector<codemap_symbol_info> &out)
{
	std::vector<std::string> lines;
	std::istringstream ss(content);
	std::string line;
	while (std::getline(ss, line)) {
		lines.push_back(line);
	}
	fallback_find_symbols_markdown(lines, min_lines, out);
}

void fallback_find_symbols_python(std::span<const std::string> lines, int min_lines, std::vector<codemap_symbol_info> &out)
{
	static const re2::RE2 py_func_regex(R"(^\s*def\s+([a-zA-Z_]\w*)\s*\()");
	static const re2::RE2 py_class_regex(R"(^\s*class\s+([a-zA-Z_]\w*))");

	for (size_t i = 0; i < lines.size(); ++i) {
		if ((i & 63) == 0 && project_manager::get_instance().is_exiting()) {
			return;
		}
		int line_num = static_cast<int>(i + 1);
		std::string name;
		if (re2::RE2::PartialMatch(lines[i], py_class_regex, &name) ||
		    re2::RE2::PartialMatch(lines[i], py_func_regex, &name)) {
			if (1 >= min_lines) {
				out.push_back({name, name, "Function", line_num, line_num, 1, 0, ""});
			}
		}
	}
}

void fallback_find_symbols_python(std::span<const std::string> lines, std::vector<codemap_symbol_info> &out)
{
	fallback_find_symbols_python(lines, 1, out);
}

void fallback_find_symbols_verilog(std::span<const std::string> lines, int min_lines, std::vector<codemap_symbol_info> &out)
{
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
	}
}

void fallback_find_symbols_verilog(std::span<const std::string> lines, std::vector<codemap_symbol_info> &out)
{
	fallback_find_symbols_verilog(lines, 1, out);
}

void fallback_find_symbols_cpp(std::span<const std::string> lines, int min_lines, std::vector<codemap_symbol_info> &out)
{
	languages::cpp::symbol_extractor::extract_symbols(lines, min_lines, out);
}

void fallback_find_symbols_cpp(std::span<const std::string> lines, std::vector<codemap_symbol_info> &out)
{
	fallback_find_symbols_cpp(lines, 1, out);
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
	if (is_markdown_file(safe_path)) {
		fallback_find_symbols_markdown(lines, min_lines, out);
	} else if (ext == ".py") {
		fallback_find_symbols_python(lines, min_lines, out);
	} else if (ext == ".sv" || ext == ".svh" || ext == ".v" || ext == ".vh") {
		fallback_find_symbols_verilog(lines, min_lines, out);
	} else {
		fallback_find_symbols_cpp(lines, min_lines, out);
	}
}

} // namespace tools
