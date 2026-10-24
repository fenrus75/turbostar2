#pragma once

#include <optional>
#include <string_view>

namespace languages::cpp
{

/**
 * @brief Retrieves standard C and C++ library function signatures for common standard functions.
 * Covers stdio, stdlib, string, memory, POSIX file I/O, and <algorithm> / <utility> / <memory> utilities.
 *
 * @param symbol_name The function identifier (e.g. "printf", "snprintf", "memcpy", "std::make_unique").
 * @return A string view of the full canonical signature, or std::nullopt if not a known builtin.
 */
std::optional<std::string_view> get_builtin_signature(std::string_view symbol_name) noexcept;

} // namespace languages::cpp
