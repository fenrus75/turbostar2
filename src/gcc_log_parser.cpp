#include "gcc_log_parser.h"
#include <ctre.hpp>
#include <filesystem>
#include "config_manager.h"
#include "fs_utils.h"

namespace fs = std::filesystem;

// Parses a single line of compiler output using compile-time regular expression.
// Pattern: <filename>:<line>:<col>: <severity>: <message>
// Supports non-greedy filename matching to handle spaces and Windows drive letters (C:/...),
// extracts 1-based line and column coordinates, matches severity (fatal error, error, warning),
// and captures the remaining error message text.
// If the line matches the GCC/Clang diagnostic format, resolves relative filepaths against the
// configured build directory, converts 1-based coordinates to 0-based editor buffer coordinates,
// and verifies that the file exists on disk before recording the error.
void gcc_log_parser::parse_line(const std::string &log_line, int output_line, std::vector<build_error> &out_errors)
{
	static constexpr auto error_pattern =
	    ctre::match<R"(^(.*?):([0-9]+):([0-9]+): (fatal error|error|warning): (.*)$)">;

	if (auto m = error_pattern(log_line); m) {
		build_error err;

		std::string file_match(m.get<1>().to_view());
		int line_num = m.get<2>().to_number<int>();
		int col_num = m.get<3>().to_number<int>();
		std::string_view severity_match = m.get<4>().to_view();
		std::string message_match(m.get<5>().to_view());

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
		err.message = std::move(message_match);
		err.output_buffer_line = output_line;

		std::error_code ec;
		if (fs::exists(err.filepath, ec) && !ec) {
			out_errors.push_back(err);
		}
	}
}
