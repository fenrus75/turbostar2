#pragma once

#include <string>

namespace languages
{

/**
 * @brief Represents an extracted token identifier with source position within a document.
 */
struct source_token {
	std::string name;
	int line{0}; // 1-based line number in file
	int col{0};  // 0-based character column offset in line
};

} // namespace languages
