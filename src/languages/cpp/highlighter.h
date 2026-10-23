#pragma once

#include "highlighter/syntax_highlighter.h"

namespace languages::cpp
{

/**
 * @brief C and C++ syntax highlighter.
 *
 * Applies syntax highlighting to individual editor line objects, classifying
 * preprocessor directives, string/character literals, comments, and C/C++ keywords.
 */
class highlighter : public syntax_highlighter
{
      public:
	highlighter() = default;
	~highlighter() override = default;

	bool supports_file(const std::string &filename) const override;
	bool supports_language(const std::string &lang) const override;
	void highlight(std::shared_ptr<line> l) override;
};

} // namespace languages::cpp
