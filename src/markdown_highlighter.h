#pragma once

#include "syntax_highlighter.h"

class markdown_highlighter : public syntax_highlighter
{
      public:
	markdown_highlighter() = default;
	~markdown_highlighter() override = default;

	bool supports_file(const std::string &filename) const override;
	void highlight(std::shared_ptr<line> l) override;
};
