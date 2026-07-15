#pragma once

#include "highlighter/syntax_highlighter.h"
#include <re2/re2.h>
#include <re2/stringpiece.h>
#include <memory>

class json_highlighter : public syntax_highlighter
{
      public:
	json_highlighter() = default;
	~json_highlighter() override = default;

	bool supports_file(const std::string &filename) const override;
	bool supports_language(const std::string &lang) const override;
	void highlight(std::shared_ptr<line> l) override;
};
