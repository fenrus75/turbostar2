#pragma once

#include "syntax_highlighter.h"
#include <regex>

class cpp_highlighter : public syntax_highlighter
{
      public:
	cpp_highlighter() = default;
	~cpp_highlighter() override = default;

	bool supports_file(const std::string &filename) const override;
	void highlight(std::shared_ptr<line> l) override;
};
