#pragma once

#include <cstdint>

/**
 * @brief Categorization of characters for syntax highlighting.
 */
enum class syntax_attribute : uint8_t { 
	normal = 0, 
	keyword = 1, 
	comment = 2, 
	string_literal = 3,
	heading = 4,
	bold = 5,
	italic = 6,
	list_item = 7
};
