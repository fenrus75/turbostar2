#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include "fs_regexp_lines.h"
#include "../../fs_utils.h"

namespace tools
{

fs_regexp_lines_tool::fs_regexp_lines_tool(fs_regexp_lines_args args) : args_(std::move(args))
{
}

bool fs_regexp_lines_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string &out_error) const
{
	re2::RE2::Options options;
	options.set_max_mem(8 * 1024 * 1024); // 8 MB cap
	if (args_.case_insensitive) {
		options.set_case_sensitive(false);
	}
	compiled_regex_ = std::make_unique<re2::RE2>(args_.pattern, options);
	if (!compiled_regex_->ok()) {
		out_error = "Invalid regular expression: " + compiled_regex_->error();
		return false;
	}
	return true;
}

std::string fs_regexp_lines_tool::escape_markdown(const std::string &text) const
{
	std::string result;
	for (size_t i = 0; i < text.size(); ++i) {
		unsigned char c = static_cast<unsigned char>(text[i]);
		if (c == 0x1b) {
			if (i + 1 < text.size() && text[i + 1] == '[') {
				i += 2;
				while (i < text.size() && (text[i] < 0x40 || text[i] > 0x7e)) {
					i++;
				}
			}
			continue;
		}
		if (c == '|') {
			result += "&#124;";
		} else if (c < 32 && c != '\t') {
			// Strip control characters
		} else if (c == 127) {
			// Strip DEL
		} else {
			result += c;
		}
	}
	return result;
}

std::string fs_regexp_lines_tool::format_line(size_t line_number, const std::string &content) const
{
	return "| " + std::to_string(line_number) + " | " + escape_markdown(content) + " |\n";
}

std::string fs_regexp_lines_tool::execute(agentlib::tool_context &ctx)
{
	if (!compiled_regex_)
		return "Error: Regex not compiled.";

	if (!args_.pattern.empty()) {
		auto &patterns = ctx.recent_grep_patterns;
		patterns.erase(std::remove(patterns.begin(), patterns.end(), args_.pattern), patterns.end());
		patterns.push_front(args_.pattern);
		if (patterns.size() > 5) {
			patterns.pop_back();
		}
	}

	std::stringstream ss;
	ss << "| Line Number | Content |\n";
	ss << "| ----------- | ------- |\n";

	size_t match_count = 0;
	const size_t MAX_MATCHES = 1000;

	// 1. Try reading from active editor document first
	if (ctx.doc_provider) {
		auto doc_snapshot = ctx.doc_provider->get_open_document(args_.safe_path);
		if (doc_snapshot) {
			size_t total_lines = doc_snapshot->get_line_count();
			for (size_t i = 0; i < total_lines; ++i) {
				std::string line_text = doc_snapshot->get_line_text(i);
				if (re2::RE2::PartialMatch(line_text, *compiled_regex_)) {
					ss << format_line(i + 1, line_text);
					match_count++;
					if (match_count >= MAX_MATCHES)
						break;
				}
			}
			if (match_count == 0)
				return "No matches found.";
			if (match_count >= MAX_MATCHES)
				ss << "| ... | *Maximum of " << MAX_MATCHES << " matches reached* |\n";
			std::string out = "# Number of matches: " + std::to_string(match_count) + "\n\n" + ss.str();
			return fs_utils::wrap_prompt_untrusted_data_tag("regex_matches", out);
		}
	}

	// 2. Try VFS if it's a URI
	if (args_.safe_path.find("://") != std::string::npos) {
		auto* vfs = ctx.fs_security.get_vfs();
		if (!vfs || !vfs->exists(args_.safe_path)) {
			return "Error: Virtual file or directory not found or not mounted.";
		}

		auto view_opt = vfs->read_file(args_.safe_path);
		if (!view_opt) {
			return "Error: Could not open virtual file for reading.";
		}

		std::string_view view = view_opt.value()->view();

		if (view.size() > 50 * 1024 * 1024) {
			return "Error: File is too large (>50MB) to read directly.";
		}

		// Binary check
		size_t check_len = std::min<size_t>(view.size(), 4096);
		for (size_t i = 0; i < check_len; ++i) {
			unsigned char b = static_cast<unsigned char>(view[i]);
			if (b == 0 || (b < 32 && b != 9 && b != 10 && b != 11 && b != 12 && b != 13 && b != 27) || b == 127) {
				return "Error: File appears to be binary. Cannot run regex on binary data.";
			}
		}

		size_t current_line = 1;
		size_t start_pos = 0;

		while (start_pos < view.length()) {
			size_t end_pos = view.find('\n', start_pos);
			std::string_view line_view =
			    (end_pos == std::string_view::npos) ? view.substr(start_pos) : view.substr(start_pos, end_pos - start_pos);

			std::string line(line_view);
			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}

			if (re2::RE2::PartialMatch(line, *compiled_regex_)) {
				ss << format_line(current_line, line);
				match_count++;
				if (match_count >= MAX_MATCHES)
					break;
			}

			start_pos = (end_pos == std::string_view::npos) ? view.length() : end_pos + 1;
			current_line++;
		}

		if (match_count == 0)
			return "No matches found.";
		if (match_count >= MAX_MATCHES)
			ss << "| ... | *Maximum of " << MAX_MATCHES << " matches reached* |\n";

		std::string final_output = "# Number of matches: " + std::to_string(match_count) + "\n\n" + ss.str();
		return fs_utils::wrap_prompt_untrusted_data_tag("regex_matches", final_output);
	}

	// 3. Fallback to direct disk access
	if (!fs_utils::is_regular_file(args_.safe_path)) {
		return "Error: Path does not exist or is not a regular file.";
	}

	struct stat sb;
	if (stat(args_.safe_path.c_str(), &sb) == 0 && sb.st_size > 50 * 1024 * 1024) {
		return "Error: File is too large (>50MB) to read directly.";
	}

	if (fs_utils::is_binary_file(args_.safe_path)) {
		return "Error: File appears to be binary. Cannot run regex on binary data.";
	}

	std::ifstream file(args_.safe_path, std::ios::binary);
	if (!file.is_open()) {
		return "Error: Could not open file for reading.";
	}

	std::string line;
	size_t current_line = 1;

	while (std::getline(file, line)) {
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		if (re2::RE2::PartialMatch(line, *compiled_regex_)) {
			ss << format_line(current_line, line);
			match_count++;
			if (match_count >= MAX_MATCHES)
				break;
		}
		current_line++;
	}

	if (match_count == 0)
		return "No matches found.";
	if (match_count >= MAX_MATCHES)
		ss << "| ... | *Maximum of " << MAX_MATCHES << " matches reached* |\n";

	std::string final_output = "# Number of matches: " + std::to_string(match_count) + "\n\n" + ss.str();
	return fs_utils::wrap_prompt_untrusted_data_tag("regex_matches", final_output);
}

} // namespace tools
