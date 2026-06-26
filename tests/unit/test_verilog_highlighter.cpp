#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include <memory>
#include <vector>
#include "line.h"
#include "verilog_highlighter.h"

void test_verilog_file_support()
{
	verilog_highlighter hl;
	assert(hl.supports_file("design.v") == true);
	assert(hl.supports_file("tb.sv") == true);
	assert(hl.supports_file("defines.vh") == true);
	assert(hl.supports_file("package.svh") == true);
	assert(hl.supports_file("main.cpp") == false);
	assert(hl.supports_file("script.py") == false);
}

void test_verilog_keywords()
{
	verilog_highlighter hl;
	auto l = std::make_shared<line>("module my_module (input wire a);");
	hl.highlight(l);

	// "module" is keyword (pos 0-5)
	assert(l->get_attribute(0) == syntax_attribute::keyword);
	assert(l->get_attribute(5) == syntax_attribute::keyword);
	assert(l->get_attribute(6) == syntax_attribute::normal);

	// "input" is keyword (pos 18-22)
	assert(l->get_attribute(18) == syntax_attribute::keyword);
	assert(l->get_attribute(22) == syntax_attribute::keyword);

	// "wire" is keyword (pos 24-27)
	assert(l->get_attribute(24) == syntax_attribute::keyword);
	assert(l->get_attribute(27) == syntax_attribute::keyword);
	assert(l->get_attribute(28) == syntax_attribute::normal);
}

void test_verilog_comments()
{
	verilog_highlighter hl;
	
	// Test line comment
	auto l1 = std::make_shared<line>("assign a = b; // line comment");
	hl.highlight(l1);
	assert(l1->get_attribute(0) == syntax_attribute::keyword); // "assign"
	assert(l1->get_attribute(14) == syntax_attribute::comment); // "//"
	assert(l1->get_attribute(28) == syntax_attribute::comment);

	// Test single-line block comment
	auto l2 = std::make_shared<line>("reg /* temporary */ r;");
	hl.highlight(l2);
	assert(l2->get_attribute(0) == syntax_attribute::keyword); // "reg"
	assert(l2->get_attribute(4) == syntax_attribute::comment); // "/*"
	assert(l2->get_attribute(18) == syntax_attribute::comment); // "*/"
	assert(l2->get_attribute(20) == syntax_attribute::normal); // "r"
}

void test_verilog_strings()
{
	verilog_highlighter hl;
	auto l = std::make_shared<line>("initial $display(\"hello world\"); // comment");
	hl.highlight(l);

	// "initial" is keyword (pos 0-6)
	assert(l->get_attribute(0) == syntax_attribute::keyword);
	assert(l->get_attribute(6) == syntax_attribute::keyword);

	// String literal (pos 17-29)
	assert(l->get_attribute(17) == syntax_attribute::string_literal);
	assert(l->get_attribute(29) == syntax_attribute::string_literal);

	// Line comment (pos 33-42)
	assert(l->get_attribute(32) == syntax_attribute::normal);
	assert(l->get_attribute(33) == syntax_attribute::comment);
	assert(l->get_attribute(42) == syntax_attribute::comment);
}

int main()
{
	test_watchdog::setup_watchdog(30);
	test_verilog_file_support();
	test_verilog_keywords();
	test_verilog_comments();
	test_verilog_strings();
	std::cout << "All verilog highlighter tests passed!" << std::endl;
	return 0;
}
