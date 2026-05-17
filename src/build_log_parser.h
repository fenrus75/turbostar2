#pragma once

#include <string>
#include <vector>
#include "event_queue.h"

/**
 * @brief Base class for parsing build logs into build_error structures.
 */
class build_log_parser
{
      public:
	virtual ~build_log_parser() = default;

	/**
	 * @brief Parses a single line of output and extracts errors.
	 * @param line The raw line of text.
	 * @param output_line The current line number in the output document.
	 * @param out_errors Vector to append found errors to.
	 */
	virtual void parse_line(const std::string &line, int output_line, std::vector<build_error> &out_errors) = 0;
};
