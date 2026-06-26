#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include <memory>
#include <vector>
#include "line.h"
#include "cpp_highlighter.h"

void test_cpp_file_support()
{
	cpp_highlighter hl;
	assert(hl.supports_file("main.cpp") == true);
	assert(hl.supports_file("helper.h") == true);
	assert(hl.supports_file("core.hpp") == true);
	assert(hl.supports_file("source.cxx") == true);
	assert(hl.supports_file("design.v") == false);
}

void test_cpp_keywords()
{
	cpp_highlighter hl;
	auto l = std::make_shared<line>("const int val = 42; return val;");
	hl.highlight(l);

	// "const" keyword (pos 0-4)
	assert(l->get_attribute(0) == syntax_attribute::keyword);
	assert(l->get_attribute(4) == syntax_attribute::keyword);
	assert(l->get_attribute(5) == syntax_attribute::normal); // space

	// "int" keyword (pos 6-8)
	assert(l->get_attribute(6) == syntax_attribute::keyword);
	assert(l->get_attribute(8) == syntax_attribute::keyword);
	assert(l->get_attribute(9) == syntax_attribute::normal);

	// "return" keyword (pos 20-25)
	assert(l->get_attribute(20) == syntax_attribute::keyword);
	assert(l->get_attribute(25) == syntax_attribute::keyword);
}

void test_cpp_preprocessor()
{
	cpp_highlighter hl;
	auto l = std::make_shared<line>("  #include <iostream>");
	hl.highlight(l);

	// Leading spaces are normal (pos 0-1)
	assert(l->get_attribute(0) == syntax_attribute::normal);
	assert(l->get_attribute(1) == syntax_attribute::normal);

	// "#include" directive is keyword (pos 2-9)
	assert(l->get_attribute(2) == syntax_attribute::keyword); // '#'
	assert(l->get_attribute(9) == syntax_attribute::keyword); // 'e'
	assert(l->get_attribute(10) == syntax_attribute::normal); // space
}

void test_cpp_strings_and_chars()
{
	cpp_highlighter hl;
	auto l = std::make_shared<line>("std::string s = \"hello world\"; char c = 'x';");
	hl.highlight(l);

	// "std" and "string" are keywords
	assert(l->get_attribute(0) == syntax_attribute::keyword);
	assert(l->get_attribute(5) == syntax_attribute::keyword);

	// String literal (pos 16-28)
	assert(l->get_attribute(16) == syntax_attribute::string_literal); // '"'
	assert(l->get_attribute(28) == syntax_attribute::string_literal); // '"'
	assert(l->get_attribute(29) == syntax_attribute::normal); // ';'

	// Char literal (pos 40-42)
	assert(l->get_attribute(40) == syntax_attribute::string_literal); // '\''
	assert(l->get_attribute(41) == syntax_attribute::string_literal); // 'x'
	assert(l->get_attribute(42) == syntax_attribute::string_literal); // '\''
}

void test_cpp_comments()
{
	cpp_highlighter hl;

	// Line comment (//)
	auto l1 = std::make_shared<line>("int x = 0; // comment here");
	hl.highlight(l1);
	assert(l1->get_attribute(0) == syntax_attribute::keyword); // "int"
	assert(l1->get_attribute(11) == syntax_attribute::comment); // "/"
	assert(l1->get_attribute(25) == syntax_attribute::comment); // "e"

	// Block comment (/* ... */)
	auto l2 = std::make_shared<line>("int /* temporary */ y = 1;");
	hl.highlight(l2);
	assert(l2->get_attribute(4) == syntax_attribute::comment); // "/"
	assert(l2->get_attribute(18) == syntax_attribute::comment); // "/"
	assert(l2->get_attribute(20) == syntax_attribute::normal); // "y"
}

void test_cpp_collisions()
{
	cpp_highlighter hl;

	// Keywords inside string should NOT be marked as keywords
	auto l1 = std::make_shared<line>("std::string s = \"return value is true\";");
	hl.highlight(l1);
	// pos 17-22 is "return", should be string_literal
	assert(l1->get_attribute(17) == syntax_attribute::string_literal);
	assert(l1->get_attribute(22) == syntax_attribute::string_literal);
	// pos 32-35 is "true", should be string_literal
	assert(l1->get_attribute(32) == syntax_attribute::string_literal);
	assert(l1->get_attribute(35) == syntax_attribute::string_literal);

	// Keywords inside comment should NOT be marked as keywords
	auto l2 = std::make_shared<line>("int x = 0; // this is int class friend");
	hl.highlight(l2);
	// pos 23-25 is "int", should be comment
	assert(l2->get_attribute(23) == syntax_attribute::comment);
	// pos 27-31 is "class", should be comment
	assert(l2->get_attribute(27) == syntax_attribute::comment);
}

int main()
{
	test_watchdog::setup_watchdog(30);
	test_cpp_file_support();
	test_cpp_keywords();
	test_cpp_preprocessor();
	test_cpp_strings_and_chars();
	test_cpp_comments();
	test_cpp_collisions();
	std::cout << "All C++ highlighter tests passed successfully!" << std::endl;
	return 0;
}
