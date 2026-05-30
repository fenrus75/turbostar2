#include <cassert>
#include <iostream>
#include <memory>
#include <vector>
#include "line.h"
#include "markdown_highlighter.h"

void test_markdown_supports_file()
{
	markdown_highlighter hl;
	assert(hl.supports_file("README.md"));
	assert(hl.supports_file("README.MD"));
	assert(hl.supports_file("README.markdown"));
	assert(hl.supports_file("README.MARKDOWN"));
	assert(!hl.supports_file("README.txt"));
}

void test_markdown_heading()
{
	markdown_highlighter hl;
	auto l = std::make_shared<line>("# Heading 1");
	hl.highlight(l);

	// The first character '#' is heading
	assert(l->get_attribute(0) == syntax_attribute::heading);
	// The rest of the line is normal
	assert(l->get_attribute(1) == syntax_attribute::normal);
	assert(l->get_attribute(2) == syntax_attribute::normal);

	auto l2 = std::make_shared<line>("## Heading 2");
	hl.highlight(l2);
	assert(l2->get_attribute(0) == syntax_attribute::heading);
	assert(l2->get_attribute(1) == syntax_attribute::heading);
	assert(l2->get_attribute(2) == syntax_attribute::normal);
}

void test_markdown_bold()
{
	markdown_highlighter hl;
	auto l = std::make_shared<line>("This is **bold** text");
	hl.highlight(l);

	// "**bold**" is at char index 8 to 15
	assert(l->get_attribute(7) == syntax_attribute::normal);
	assert(l->get_attribute(8) == syntax_attribute::bold);
	assert(l->get_attribute(15) == syntax_attribute::bold);
	assert(l->get_attribute(16) == syntax_attribute::normal);
}

void test_markdown_bold_utf8()
{
	markdown_highlighter hl;
	// "🦀" is a 4-byte character. Let's place it inside and outside bold.
	auto l = std::make_shared<line>("🦀 **🦀 bold 🦀** 🦀");
	hl.highlight(l);

	// First 🦀 is normal (char index 0)
	assert(l->get_attribute(0) == syntax_attribute::normal);
	// Space is normal (char index 1)
	assert(l->get_attribute(1) == syntax_attribute::normal);
	
	// Bold starts at char index 2 (**) and spans 12 chars: **🦀 bold 🦀**
	// Char indices: 2 to 13 inclusive
	assert(l->get_attribute(2) == syntax_attribute::bold);
	assert(l->get_attribute(4) == syntax_attribute::bold); // 🦀
	assert(l->get_attribute(13) == syntax_attribute::bold); // last *
	assert(l->get_attribute(14) == syntax_attribute::normal); // trailing space
	assert(l->get_attribute(15) == syntax_attribute::normal); // trailing 🦀
}

void test_markdown_list_items()
{
	markdown_highlighter hl;
	auto l = std::make_shared<line>("- Item 1");
	hl.highlight(l);
	assert(l->get_attribute(0) == syntax_attribute::list_item);
	assert(l->get_attribute(1) == syntax_attribute::normal);

	auto l2 = std::make_shared<line>("  * Item 2");
	hl.highlight(l2);
	// list item is at char index 2
	assert(l2->get_attribute(0) == syntax_attribute::normal);
	assert(l2->get_attribute(2) == syntax_attribute::list_item);
}

int main()
{
	test_markdown_supports_file();
	test_markdown_heading();
	test_markdown_bold();
	test_markdown_bold_utf8();
	test_markdown_list_items();

	std::cout << "All markdown highlighter tests passed!" << std::endl;
	return 0;
}
