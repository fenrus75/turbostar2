// Tested source file: src/highlighter/highlighter_registry.cpp
#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include <memory>
#include "highlighter/highlighter_registry.h"
#include "highlighter/cpp_highlighter.h"
#include "highlighter/python_highlighter.h"
#include "highlighter/json_highlighter.h"
#include "highlighter/html_highlighter.h"
#include "highlighter/markdown_highlighter.h"
#include "highlighter/verilog_highlighter.h"
#include "line.h"

void test_registry_instance()
{
	auto &reg1 = highlighter_registry::get_instance();
	auto &reg2 = highlighter_registry::get_instance();
	assert(&reg1 == &reg2);
}

void test_registry_file_dispatch()
{
	auto &reg = highlighter_registry::get_instance();

	// C++ files
	auto cpp_hl = reg.get_highlighter_for_file("main.cpp");
	assert(cpp_hl != nullptr);
	assert(dynamic_cast<cpp_highlighter *>(cpp_hl.get()) != nullptr);

	auto h_hl = reg.get_highlighter_for_file("header.hpp");
	assert(dynamic_cast<cpp_highlighter *>(h_hl.get()) != nullptr);

	// Python files
	auto py_hl = reg.get_highlighter_for_file("script.py");
	assert(py_hl != nullptr);
	assert(dynamic_cast<python_highlighter *>(py_hl.get()) != nullptr);

	// JSON files
	auto json_hl = reg.get_highlighter_for_file("package.json");
	assert(json_hl != nullptr);
	assert(dynamic_cast<json_highlighter *>(json_hl.get()) != nullptr);

	// HTML files
	auto html_hl = reg.get_highlighter_for_file("index.html");
	assert(html_hl != nullptr);
	assert(dynamic_cast<html_highlighter *>(html_hl.get()) != nullptr);

	// Markdown files
	auto md_hl = reg.get_highlighter_for_file("README.md");
	assert(md_hl != nullptr);
	assert(dynamic_cast<markdown_highlighter *>(md_hl.get()) != nullptr);

	// Verilog files
	auto v_hl = reg.get_highlighter_for_file("alu.sv");
	assert(v_hl != nullptr);
	assert(dynamic_cast<verilog_highlighter *>(v_hl.get()) != nullptr);

	// Unknown / fallback file
	auto def_hl = reg.get_highlighter_for_file("data.unknown_ext");
	assert(def_hl != nullptr);
	auto l = std::make_shared<line>("plain text content");
	def_hl->highlight(l);
	for (int i = 0; i < l->length_in_chars(); ++i) {
		assert(l->get_attribute(i) == syntax_attribute::normal);
	}
}

void test_registry_language_dispatch()
{
	auto &reg = highlighter_registry::get_instance();

	assert(dynamic_cast<cpp_highlighter *>(reg.get_highlighter_for_language("cpp").get()) != nullptr);
	assert(dynamic_cast<python_highlighter *>(reg.get_highlighter_for_language("python").get()) != nullptr);
	assert(dynamic_cast<json_highlighter *>(reg.get_highlighter_for_language("json").get()) != nullptr);
	assert(dynamic_cast<html_highlighter *>(reg.get_highlighter_for_language("html").get()) != nullptr);
	assert(dynamic_cast<markdown_highlighter *>(reg.get_highlighter_for_language("markdown").get()) != nullptr);
	assert(dynamic_cast<verilog_highlighter *>(reg.get_highlighter_for_language("verilog").get()) != nullptr);

	// Unsupported language returns nullptr
	assert(reg.get_highlighter_for_language("nonexistent_lang") == nullptr);
	assert(reg.get_highlighter_for_language("") == nullptr);
}

int main()
{
	test_watchdog::setup_watchdog(30);
	test_registry_instance();
	test_registry_file_dispatch();
	test_registry_language_dispatch();
	std::cout << "All highlighter_registry tests passed!" << std::endl;
	return 0;
}
