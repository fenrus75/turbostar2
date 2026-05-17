#include "gcc_log_parser.h"
#include "config_manager.h"
#include "fs_utils.h"
#include <filesystem>

namespace fs = std::filesystem;

gcc_log_parser::gcc_log_parser()
{
	// Regex matches: <filename>:<line>:<col>: <severity>: <message>
	// Capture groups: 1=file, 2=line, 3=col, 4=severity, 5=message
	error_regex_ = std::regex(R"(^([^:\s]+):([0-9]+):([0-9]+): (error|warning): (.*)$)");
}

void gcc_log_parser::parse_line(const std::string &line, int output_line, std::vector<build_error> &out_errors)
{
	std::smatch match;
	if (std::regex_search(line, match, error_regex_)) {
		build_error err;
		
		fs::path p(match[1].str());
		if (!p.is_absolute()) {
			std::string build_dir = config_manager::get_instance().get_build_directory();
			p = fs_utils::safe_absolute(fs::path(build_dir) / p);
		}
		err.filepath = p.string();
		
		err.line = std::stoi(match[2].str()) - 1; // 1-based to 0-based
		err.column = std::stoi(match[3].str()) - 1;
		err.is_warning = (match[4].str() == "warning");
		err.message = match[5].str();
		err.output_buffer_line = output_line;

		std::error_code ec;
		if (fs::exists(err.filepath, ec)) {
			out_errors.push_back(err);
		}
	}
}
