#pragma once

#include "build_log_parser.h"

/**
 * @brief Parses GCC/Clang style error strings: "file:line:col: [error|warning]: message"
 */
class gcc_log_parser : public build_log_parser
{
      public:
	gcc_log_parser() = default;
	void parse_line(const std::string &log_line, int output_line, std::vector<build_error> &out_errors) override;
};
