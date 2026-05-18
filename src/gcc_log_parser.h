#pragma once

#include "build_log_parser.h"
#include <memory>
#include <re2/re2.h>

/**
 * @brief Parses GCC/Clang style error strings: "file:line:col: [error|warning]: message"
 */
class gcc_log_parser : public build_log_parser
{
      public:
	gcc_log_parser();
	void parse_line(const std::string &line, int output_line, std::vector<build_error> &out_errors) override;

      private:
	std::unique_ptr<re2::RE2> error_regex_;
};
