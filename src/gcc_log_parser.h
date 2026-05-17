#pragma once

#include "build_log_parser.h"
#include <regex>

/**
 * @brief Parses GCC/Clang style error strings: "file:line:col: [error|warning]: message"
 */
class gcc_log_parser : public build_log_parser
{
      public:
	gcc_log_parser();
	void parse_line(const std::string &line, int output_line, std::vector<build_error> &out_errors) override;

      private:
	std::regex error_regex_;
};
