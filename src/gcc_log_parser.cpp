#include "gcc_log_parser.h"
#include "config_manager.h"
#include "fs_utils.h"
#include <filesystem>

namespace fs = std::filesystem;

gcc_log_parser::gcc_log_parser()
{
	// Regex matches: <filename>:<line>:<col>: <severity>: <message>
	// Capture groups: 1=file, 2=line, 3=col, 4=severity, 5=message
	error_regex_ = std::make_unique<re2::RE2>(R"(^([^:\s]+):([0-9]+):([0-9]+): (error|warning): (.*)$)");
}

void gcc_log_parser::parse_line(const std::string &line, int output_line, std::vector<build_error> &out_errors)
{
	std::string file_match, severity_match, message_match;
	int line_num, col_num;

	if (re2::RE2::PartialMatch(line, *error_regex_, &file_match, &line_num, &col_num, &severity_match, &message_match)) {
		build_error err;
		
		fs::path p(file_match);
		if (!p.is_absolute()) {
			std::string build_dir = config_manager::get_instance().get_build_directory();
			p = fs_utils::safe_absolute(fs::path(build_dir) / p);
		}
		err.filepath = p.string();
		
		err.line = line_num - 1; // 1-based to 0-based
		err.column = col_num - 1;
		err.end_column = 0; // Highlight whole line for compilers
		err.is_warning = (severity_match == "warning");
		err.message = message_match;
		err.output_buffer_line = output_line;

		std::error_code ec;
		if (fs::exists(err.filepath, ec)) {
			out_errors.push_back(err);
		}
	}
}
