#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include "fs_regexp_lines.h"

namespace tools
{

fs_regexp_lines_tool::fs_regexp_lines_tool(fs_regexp_lines_args args) : args_(std::move(args))
{
}

bool fs_regexp_lines_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string &out_error) const
{
	compiled_regex_ = std::make_unique<re2::RE2>(args_.pattern);
	if (!compiled_regex_->ok()) {
		out_error = "Invalid regular expression: " + compiled_regex_->error();
		return false;
	}
	return true;
}

std::string fs_regexp_lines_tool::escape_markdown(const std::string &text) const
{
	std::string result;
	for (char c : text) {
		if (c == '|') {
			result += "&#124;"; // HTML entity for pipe
		} else if (c == '\r' || c == '\n') {
			// Strip newlines to avoid breaking table format
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
			return "# Number of matches: " + std::to_string(match_count) + "\n\n" + ss.str();
		}
	}

	// 2. Fallback to direct disk access
	struct stat sb;
	if (stat(args_.safe_path.c_str(), &sb) == -1) {
		return "Error: File does not exist or cannot be accessed.";
	}

	if (sb.st_size > 50 * 1024 * 1024) {
		return "Error: File is too large (>50MB) to read directly.";
	}

	std::ifstream file(args_.safe_path, std::ios::binary);
	if (!file.is_open()) {
		return "Error: Could not open file for reading.";
	}

	char buffer[4096];
	file.read(buffer, sizeof(buffer));
	size_t bytes_read = file.gcount();
	if (memchr(buffer, '\0', bytes_read) != nullptr) {
		return "Error: File appears to be binary. Cannot run regex on binary data.";
	}

	file.clear();
	file.seekg(0);

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
	return final_output;
}

} // namespace tools
