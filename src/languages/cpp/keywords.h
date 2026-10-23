#pragma once

#include <string>
#include <string_view>

namespace languages::cpp
{

/**
 * @brief Centralized database and classification queries for C and C++ keywords,
 * primitive types, standard library containers, and token extraction filters.
 */
class keywords
{
      public:
	/**
	 * @brief Returns true if word is a standard C or C++ language keyword,
	 * alternative operator token, or common compiler builtin.
	 */
	static bool is_keyword(std::string_view word) noexcept;

	/**
	 * @brief Returns true if word is a fundamental C/C++ primitive type or
	 * standard fixed-width / pointer integer typedef (int, size_t, uint32_t, ptrdiff_t, etc.).
	 */
	static bool is_primitive_type(std::string_view word) noexcept;

	/**
	 * @brief Returns true if word can precede a function name in a C/C++ definition header
	 * (storage specifiers, return types, evaluation specifiers, or kernel attributes).
	 */
	static bool is_function_specifier(std::string_view word) noexcept;

	/**
	 * @brief Returns true if word is a standard library container, utility type, or namespace.
	 */
	static bool is_std_type_or_container(std::string_view word) noexcept;

	/**
	 * @brief Returns true if identifier should be filtered out from candidate function calls
	 * (control flow, casts, operators, builtins, macro annotations, and keywords).
	 */
	static bool is_call_candidate_denylisted(std::string_view identifier) noexcept;

	/**
	 * @brief Returns true if identifier should be filtered out from candidate user types
	 * (primitives, keywords, std containers, known 3rd-party namespaces, or ALL_CAPS macros).
	 */
	static bool is_type_candidate_denylisted(std::string_view identifier) noexcept;
};

} // namespace languages::cpp
