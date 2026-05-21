#pragma once

#include "syntax_highlighter.h"
#include <re2/re2.h>
#include <re2/stringpiece.h>
#include <memory>

class python_highlighter : public syntax_highlighter
{
      public:
	python_highlighter() = default;
	~python_highlighter() override = default;

	bool supports_file(const std::string &filename) const override;
	void highlight(std::shared_ptr<line> l) override;
};
