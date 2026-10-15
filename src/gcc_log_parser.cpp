#include "gcc_log_parser.h"
#include <filesystem>
#include "config_manager.h"
#include "fs_utils.h"

namespace fs = std::filesystem;

// Initializes regex for GCC/Clang diagnostic output lines.
// Pattern: <filename>:<line>:<col>: <severity>: <message>
// Supports non-greedy filename matching to handle spaces and Windows drive letters (C:/...),
// extracts 1-based line and column coordinates, matches severity (fatal error, error, warning),
// and captures the remaining error message text.
gcc_log_parser::gcc_log_parser()
{
	error_regex_ = std::make_unique<re2::RE2>(R"(^(.*?):([0-9]+):([0-9]+): (fatal error|error|warning): (.*)$)");
}

// Parses a single line of compiler output. If the line matches the GCC/Clang diagnostic format,
// resolves relative filepaths against the configured build directory, converts 1-based coordinates
// to 0-based editor buffer coordinates, and verifies that the file exists on disk before recording the error.
void gcc_log_parser::parse_line(const std::string &log_line, int output_line, std::vector<build_error> &out_errors)
{
	std::string file_match, severity_match, message_match;
	int line_num, col_num;

	if (re2::RE2::PartialMatch(log_line, *error_regex_, &file_match, &line_num, &col_num, &severity_match, &message_match)) {
		build_error err;

		fs::path p(file_match);
		if (!p.is_absolute()) {
			std::string build_dir = config_manager::get_instance().get_build_directory();
			p = fs_utils::safe_absolute(fs::path(build_dir) / p);
		}
		err.filepath = p.string();

		err.line = line_num - 1; // 1-based to 0-based conversion
		err.column = col_num - 1;
		err.end_column = 0; // Highlight whole line for compilers
		err.is_warning = (severity_match == "warning");
		err.message = message_match;
		err.output_buffer_line = output_line;

		std::error_code ec;
		if (fs::exists(err.filepath, ec) && !ec) {
			out_errors.push_back(err);
		}
	}
}
