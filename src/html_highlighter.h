#pragma once

#include "syntax_highlighter.h"

class html_highlighter : public syntax_highlighter
{
      public:
	html_highlighter() = default;
	~html_highlighter() override = default;

	bool supports_file(const std::string &filename) const override;
	void highlight(std::shared_ptr<line> l) override;
};
