#include "python_highlighter.h"
#include "line.h"
#include <cassert>
#include <iostream>
#include <memory>
#include <vector>

void test_python_keywords() {
	python_highlighter hl;
	auto l = std::make_shared<line>("def my_func(a): return a");
	hl.highlight(l);
	
	// "def" is keyword (pos 0-2)
	assert(l->get_attribute(0) == syntax_attribute::keyword);
	assert(l->get_attribute(1) == syntax_attribute::keyword);
	assert(l->get_attribute(2) == syntax_attribute::keyword);
	assert(l->get_attribute(3) == syntax_attribute::normal);
	
	// "return" is keyword (pos 16-21)
	assert(l->get_attribute(16) == syntax_attribute::keyword);
	assert(l->get_attribute(21) == syntax_attribute::keyword);
}

void test_python_comments() {
	python_highlighter hl;
	auto l = std::make_shared<line>("x = 1 # my comment");
	hl.highlight(l);
	
	assert(l->get_attribute(0) == syntax_attribute::normal);
	assert(l->get_attribute(6) == syntax_attribute::comment);
	assert(l->get_attribute(17) == syntax_attribute::comment);
}

void test_python_strings() {
	python_highlighter hl;
	auto l = std::make_shared<line>("s = \"hello world\" # not a comment");
	hl.highlight(l);
	
	assert(l->get_attribute(4) == syntax_attribute::string_literal);
	assert(l->get_attribute(16) == syntax_attribute::string_literal);
	
	auto l2 = std::make_shared<line>("s = 'quote' # this is a comment");
	hl.highlight(l2);
	assert(l2->get_attribute(4) == syntax_attribute::string_literal);
	assert(l2->get_attribute(10) == syntax_attribute::string_literal);
	assert(l2->get_attribute(12) == syntax_attribute::comment);
}

int main() {
	test_python_keywords();
	test_python_comments();
	test_python_strings();
	std::cout << "All python highlighter tests passed!" << std::endl;
	return 0;
}
