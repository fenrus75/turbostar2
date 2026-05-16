#pragma once

#include <memory>
#include <string>
#include "line.h"

/**
 * @brief Base class for language-specific syntax highlighters.
 */
class syntax_highlighter
{
      public:
	virtual ~syntax_highlighter() = default;

	/**
	 * @brief Returns true if this highlighter supports the given filename.
	 */
	virtual bool supports_file(const std::string &filename) const = 0;

	/**
	 * @brief Processes a single line and applies syntax attributes.
	 */
	virtual void highlight(std::shared_ptr<line> l) = 0;
};
