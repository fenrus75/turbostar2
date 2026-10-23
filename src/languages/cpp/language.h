#pragma once

#include <string_view>

namespace languages::cpp
{

/**
 * @brief Returns true if filename has a recognized C or C++ extension (.c, .cpp, .cc, .cxx, .h, .hpp).
 */
bool supports_file(std::string_view filename) noexcept;

/**
 * @brief Returns true if language identifier is recognized as C or C++.
 */
bool supports_language(std::string_view lang) noexcept;

} // namespace languages::cpp
