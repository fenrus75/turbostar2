// Tested source file: src/highlighter/html_highlighter.cpp
#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include <memory>
#include <vector>
#include "line.h"
#include "highlighter/html_highlighter.h"

void test_html_supports_file()
{
	html_highlighter hl;
	assert(hl.supports_file("index.html"));
	assert(hl.supports_file("index.HTML"));
	assert(hl.supports_file("about.htm"));
	assert(hl.supports_file("about.HTM"));
	assert(!hl.supports_file("style.css"));
	assert(!hl.supports_file("main.cpp"));
}

void test_html_basic_tag()
{
	html_highlighter hl;
	auto l = std::make_shared<line>("<html>");
	hl.highlight(l);

	// All characters in "<html>" should be keyword
	for (int i = 0; i < l->length_in_chars(); ++i) {
		assert(l->get_attribute(i) == syntax_attribute::keyword);
	}
}

void test_html_tag_attributes()
{
	html_highlighter hl;
	auto l = std::make_shared<line>("<div class=\"content\">");
	hl.highlight(l);

	// `<div class=` are keywords (chars 0 to 10)
	for (int i = 0; i <= 10; ++i) {
		assert(l->get_attribute(i) == syntax_attribute::keyword);
	}

	// `"content"` are string literals (chars 11 to 19, including quotes)
	for (int i = 11; i <= 19; ++i) {
		assert(l->get_attribute(i) == syntax_attribute::string_literal);
	}

	// `>` is keyword (char 20)
	assert(l->get_attribute(20) == syntax_attribute::keyword);
}

void test_html_comments()
{
	html_highlighter hl;
	auto l = std::make_shared<line>("<!-- Hello -->");
	hl.highlight(l);

	// Entire comment should be styled as comment
	for (int i = 0; i < l->length_in_chars(); ++i) {
		assert(l->get_attribute(i) == syntax_attribute::comment);
	}
}

void test_html_utf8()
{
	html_highlighter hl;
	// "🦀" is a 4-byte UTF-8 character (char index 5)
	auto l = std::make_shared<line>("<div>🦀</div>");
	hl.highlight(l);

	// `<div>` is keyword (chars 0 to 4)
	for (int i = 0; i <= 4; ++i) {
		assert(l->get_attribute(i) == syntax_attribute::keyword);
	}

	// `🦀` is normal text (char 5)
	assert(l->get_attribute(5) == syntax_attribute::normal);

	// `</div>` is keyword (chars 6 to 11)
	for (int i = 6; i <= 11; ++i) {
		assert(l->get_attribute(i) == syntax_attribute::keyword);
	}
}

void test_html_edge_cases()
{
	html_highlighter hl;
	assert(hl.supports_language("html"));
	assert(hl.supports_language("htm"));
	assert(!hl.supports_language("xml"));

	// Empty line
	auto l_empty = std::make_shared<line>("");
	hl.highlight(l_empty);
	assert(l_empty->length_in_chars() == 0);

	// Single quotes and self-closing tag
	auto l_self = std::make_shared<line>("<input type='text' />");
	hl.highlight(l_self);
	assert(l_self->get_attribute(0) == syntax_attribute::keyword); // '<'
	assert(l_self->get_attribute(11) == syntax_attribute::keyword); // '='
	assert(l_self->get_attribute(12) == syntax_attribute::string_literal); // '\''
	assert(l_self->get_attribute(17) == syntax_attribute::string_literal); // '\''
	assert(l_self->get_attribute(20) == syntax_attribute::keyword); // '>'

	// Unclosed tag
	auto l_unclosed = std::make_shared<line>("<div class=\"unfinished");
	hl.highlight(l_unclosed);
	assert(l_unclosed->get_attribute(0) == syntax_attribute::keyword);
	assert(l_unclosed->get_attribute(12) == syntax_attribute::string_literal);
}

int main()
{
	test_watchdog::setup_watchdog(30);
	test_html_supports_file();
	test_html_basic_tag();
	test_html_tag_attributes();
	test_html_comments();
	test_html_utf8();
	test_html_edge_cases();

	std::cout << "All HTML highlighter tests passed successfully!" << std::endl;
	return 0;
}
