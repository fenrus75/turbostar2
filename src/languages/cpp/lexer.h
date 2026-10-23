#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace languages::cpp
{

enum class token_kind : uint8_t {
	eof = 0,
	identifier,
	keyword,
	number,
	string_literal,
	character_literal,
	comment,
	preprocessor,
	l_paren,        // (
	r_paren,        // )
	l_brace,        // {
	r_brace,        // }
	l_bracket,      // [
	r_bracket,      // ]
	semicolon,      // ;
	colon,          // :
	double_colon,   // ::
	comma,          // ,
	dot,            // .
	arrow,          // ->
	tilde,          // ~
	operator_sym,   // +, -, *, /, %, &, |, ^, !, =, <, >, etc.
	other,
};

struct token {
	token_kind kind{token_kind::eof};
	std::string_view text;
	int line{1};
	int col{1};
	int brace_depth{0};
	int paren_depth{0};
	int bracket_depth{0};
};

/**
 * @brief Lightweight, zero-allocation, error-resilient C++ lexer.
 *
 * Scans C++ source buffers into a flat sequence of tokens without constructing an AST.
 * Designed for resilience against incomplete, malformed, or buggy code (e.g. unclosed
 * comments/strings, stray closing braces, unbalanced delimiters) so callers like
 * syntax highlighters and symbol extractors never crash or stall.
 */
class lexer
{
      public:
	/**
	 * @brief Tokenizes a contiguous C++ source buffer.
	 * @param source Contiguous string view into source file buffer.
	 * @return Vector of tokens referencing slices of `source`.
	 */
	static std::vector<token> tokenize(std::string_view source);

	/**
	 * @brief Converts a token_kind to its human-readable string representation.
	 */
	static std::string_view token_kind_to_string(token_kind kind) noexcept;
};

} // namespace languages::cpp
