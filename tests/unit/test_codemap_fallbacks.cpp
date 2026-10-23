// Tested source file: src/codemap_fallbacks.cpp

#include "codemap_fallbacks.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <vector>
#include "unit/test_watchdog.h"

int main()
{
	test_watchdog::setup_watchdog(30);

	std::cout << "Testing codemap_fallbacks language-specific parsers..." << std::endl;

	// 0. Test is_markdown_file extension checker
	{
		assert(tools::is_markdown_file("doc.md"));
		assert(tools::is_markdown_file("README.markdown"));
		assert(tools::is_markdown_file("notes.mdown"));
		assert(tools::is_markdown_file("spec.mkd"));
		assert(!tools::is_markdown_file("code.cpp"));
		assert(!tools::is_markdown_file("header.h"));
		assert(!tools::is_markdown_file("script.py"));
		assert(!tools::is_markdown_file("text.txt"));
		assert(!tools::is_markdown_file(""));
	}

	// 1. Test Python fallback parser
	{
		std::vector<std::string> py_lines = {
		    "# Sample python file",
		    "import os",
		    "",
		    "class Worker:",
		    "    def __init__(self):",
		    "        pass",
		    "",
		    "def run_job(x, y):",
		    "    return x + y",
		};

		// Test default min_lines overload
		std::vector<tools::codemap_symbol_info> py_symbols;
		tools::fallback_find_symbols_python(py_lines, py_symbols);

		assert(py_symbols.size() == 3);
		assert(py_symbols[0].name == "Worker");
		assert(py_symbols[0].start_line == 4);
		assert(py_symbols[1].name == "__init__");
		assert(py_symbols[1].start_line == 5);
		assert(py_symbols[2].name == "run_job");
		assert(py_symbols[2].start_line == 8);

		// Test min_lines filtering (Python 1-line symbols pruned when min_lines > 1)
		std::vector<tools::codemap_symbol_info> py_filtered;
		tools::fallback_find_symbols_python(py_lines, 5, py_filtered);
		assert(py_filtered.empty());
	}

	// 2. Test Verilog / SystemVerilog fallback parser
	{
		std::vector<std::string> sv_lines = {
		    "// SV Header",
		    "module fifo_buffer #(",
		    "    parameter WIDTH = 8",
		    ") (",
		    "    input clk,",
		    "    input rst",
		    ");",
		    "    function int get_size();",
		    "        return 16;",
		    "    endfunction",
		    "",
		    "    task reset_state();",
		    "        // reset logic",
		    "    endtask",
		    "endmodule",
		    "",
		    "interface eth_rx_if (input clk);",
		    "    logic data;",
		    "endinterface",
		    "",
		    "package cpu_pkg;",
		    "    localparam ADDR_WIDTH = 32;",
		    "endpackage",
		    "",
		    "class Transaction;",
		    "    int id;",
		    "endclass",
		};

		// Test default min_lines overload
		std::vector<tools::codemap_symbol_info> sv_symbols;
		tools::fallback_find_symbols_verilog(sv_lines, sv_symbols);

		assert(sv_symbols.size() == 6);
		assert(sv_symbols[0].name == "fifo_buffer");
		assert(sv_symbols[0].kind_str == "Module");
		assert(sv_symbols[0].start_line == 2);
		assert(sv_symbols[0].end_line == 15);

		assert(sv_symbols[1].name == "get_size");
		assert(sv_symbols[1].kind_str == "Function");
		assert(sv_symbols[1].start_line == 8);
		assert(sv_symbols[1].end_line == 10);

		assert(sv_symbols[2].name == "reset_state");
		assert(sv_symbols[2].kind_str == "Task");
		assert(sv_symbols[2].start_line == 12);
		assert(sv_symbols[2].end_line == 14);

		assert(sv_symbols[3].name == "eth_rx_if");
		assert(sv_symbols[3].kind_str == "Interface");
		assert(sv_symbols[3].start_line == 17);
		assert(sv_symbols[3].end_line == 19);

		assert(sv_symbols[4].name == "cpu_pkg");
		assert(sv_symbols[4].kind_str == "Package");
		assert(sv_symbols[4].start_line == 21);
		assert(sv_symbols[4].end_line == 23);

		assert(sv_symbols[5].name == "Transaction");
		assert(sv_symbols[5].kind_str == "Class");
		assert(sv_symbols[5].start_line == 25);
		assert(sv_symbols[5].end_line == 27);

		// Test min_lines filtering (only fifo_buffer with len 14 spans >= 10 lines)
		std::vector<tools::codemap_symbol_info> sv_filtered;
		tools::fallback_find_symbols_verilog(sv_lines, 10, sv_filtered);
		assert(sv_filtered.size() == 1);
		assert(sv_filtered[0].name == "fifo_buffer");
	}

	// 3. Test C / C++ fallback parser
	{
		std::vector<std::string> cpp_lines = {
		    "// C++ Sample",
		    "#include <vector>",
		    "",
		    "// Forward declarations - should NOT be recognized as definitions",
		    "struct ForwardPoint;",
		    "class OpaqueWidget;",
		    "enum StatusUnused : int;",
		    "",
		    "struct Point {",
		    "    int x;",
		    "    int y;",
		    "};",
		    "",
		    "enum class Status {",
		    "    Ok,",
		    "    Error",
		    "};",
		    "",
		    "// Parameters with struct/class/enum keywords - should NOT be recognized as definitions",
		    "void helper1(struct Foo *f, const struct Bar &b, struct Baz, int x) {}",
		    "void helper2(class Widget *w) {}",
		    "",
		    "// Control keywords - should NOT be recognized as functions",
		    "void control_flow_func() {",
		    "    if (true) { int a = 1; }",
		    "    while (false) { int b = 2; }",
		    "    for (int i = 0; i < 10; ++i) { int c = 3; }",
		    "    switch (1) { case 1: break; }",
		    "    try { throw 1; } catch (...) { int e = 4; }",
		    "}",
		    "",
		    "// Multi-line prototype - should be ignored",
		    "int multi_line_prototype(",
		    "    int a,",
		    "    int b",
		    ");",
		    "",
		    "#define SOME_MACRO 1",
		    "/* Block comment */",
		    "// Line comment",
		    "static",
		    "inline void",
		    "multi_spec_func(int a, // comment in param list",
		    "                int b)",
		    "{",
		    "    return;",
		    "}",
		    "",
		    "int global_boundary_var = 123;",
		    "int compute_sum(int a, int b)",
		    "{",
		    "    return a + b;",
		    "}",
		};

		// Test default min_lines overload
		std::vector<tools::codemap_symbol_info> cpp_symbols;
		tools::fallback_find_symbols_cpp(cpp_lines, cpp_symbols);

		// Verified definitions: Point, Status, helper1, helper2, control_flow_func, multi_spec_func, compute_sum
		assert(cpp_symbols.size() == 7);
		assert(cpp_symbols[0].name == "Point");
		assert(cpp_symbols[0].kind_str == "Struct");
		assert(cpp_symbols[0].start_line == 9);
		assert(cpp_symbols[0].end_line == 12);

		assert(cpp_symbols[1].name == "Status");
		assert(cpp_symbols[1].kind_str == "Enum");
		assert(cpp_symbols[1].start_line == 14);
		assert(cpp_symbols[1].end_line == 17);

		assert(cpp_symbols[2].name == "helper1");
		assert(cpp_symbols[2].kind_str == "Function");

		assert(cpp_symbols[3].name == "helper2");
		assert(cpp_symbols[3].kind_str == "Function");

		assert(cpp_symbols[4].name == "control_flow_func");
		assert(cpp_symbols[4].kind_str == "Function");

		assert(cpp_symbols[5].name == "multi_spec_func");
		assert(cpp_symbols[5].kind_str == "Function");
		// Backward walk should capture 'static' and 'inline void'
		assert(cpp_symbols[5].start_line == 41);
		assert(cpp_symbols[5].end_line == 47);

		assert(cpp_symbols[6].name == "compute_sum");
		assert(cpp_symbols[6].kind_str == "Function");
		assert(cpp_symbols[6].start_line == 50);
		assert(cpp_symbols[6].end_line == 53);

		// Test min_lines filter
		std::vector<tools::codemap_symbol_info> cpp_filtered;
		tools::fallback_find_symbols_cpp(cpp_lines, 6, cpp_filtered);
		// control_flow_func (7 lines) and multi_spec_func (7 lines) qualify
		assert(cpp_filtered.size() == 2);
		assert(cpp_filtered[0].name == "control_flow_func");
		assert(cpp_filtered[1].name == "multi_spec_func");

		// Test that comments and string literals are masked and do not produce false symbols
		// or corrupt brace depth tracking
		std::vector<std::string> comment_string_lines = {
			"/*",
			"class FakeCommentClass {",
			"    void fake_method() {}",
			"};",
			"*/",
			"// struct FakeCommentStruct { int a; };",
			"const char *code = R\"(class FakeStringClass { int x; };)\";",
			"void real_function()",
			"{",
			"    char open_brace = '{';",
			"    // } fake closing brace in comment",
			"    std::string s = \"{\";",
			"    int val = 42;",
			"}"
		};
		std::vector<tools::codemap_symbol_info> masked_syms;
		tools::fallback_find_symbols_cpp(comment_string_lines, masked_syms);
		assert(masked_syms.size() == 1);
		assert(masked_syms[0].name == "real_function");
		assert(masked_syms[0].start_line == 8);
		assert(masked_syms[0].end_line == 14);

		// Test function specifier backward walk with standard types like ptrdiff_t and char32_t
		std::vector<std::string> spec_lines = {
			"ptrdiff_t",
			"calculate_diff(int x, int y)",
			"{",
			"    return x - y;",
			"}"
		};
		std::vector<tools::codemap_symbol_info> diff_syms;
		tools::fallback_find_symbols_cpp(spec_lines, diff_syms);
		assert(diff_syms.size() == 1);
		assert(diff_syms[0].name == "calculate_diff");
		assert(diff_syms[0].start_line == 1);
		assert(diff_syms[0].end_line == 5);
	}

	// 4. Test Markdown headings parser (both span lines and string wrapper)
	{
		std::vector<std::string> md_lines = {
		    "# Title",
		    "",
		    "Intro text with #stray_hash and not a heading.",
		    "",
		    "## Section 1 ##",
		    "Details",
		    "",
		    "### Subsection 1.1",
		    "More details",
		    "",
		    "####### Too Many Hashes Ignored",
		    "## ",
		    "",
		    "## Section 2",
		    "Closing",
		};

		// Test default min_lines overload
		std::vector<tools::codemap_symbol_info> md_symbols;
		tools::fallback_find_symbols_markdown(md_lines, md_symbols);

		assert(md_symbols.size() == 5);
		assert(md_symbols[0].name == "Title");
		assert(md_symbols[0].depth == 0);
		assert(md_symbols[0].start_line == 1);

		assert(md_symbols[1].name == "Section 1");
		assert(md_symbols[1].depth == 1);
		assert(md_symbols[1].start_line == 5);

		assert(md_symbols[2].name == "Subsection 1.1");
		assert(md_symbols[2].depth == 2);
		assert(md_symbols[2].start_line == 8);

		assert(md_symbols[3].name == "##");
		assert(md_symbols[3].depth == 1);
		assert(md_symbols[3].start_line == 12);

		assert(md_symbols[4].name == "Section 2");
		assert(md_symbols[4].depth == 1);
		assert(md_symbols[4].start_line == 14);

		// Test min_lines filtering (headings are 1-line anchors; min_lines > 1 yields empty)
		std::vector<tools::codemap_symbol_info> md_filtered;
		tools::fallback_find_symbols_markdown(md_lines, 2, md_filtered);
		assert(md_filtered.empty());

		// Also verify backward-compatible parse_markdown_headings
		std::string md_doc = "# Title\n\nIntro text.\n\n## Section 1\nDetails\n";
		std::vector<tools::codemap_symbol_info> wrapper_symbols;
		tools::parse_markdown_headings(md_doc, 1, wrapper_symbols);
		assert(wrapper_symbols.size() == 2);
		assert(wrapper_symbols[0].name == "Title");
		assert(wrapper_symbols[1].name == "Section 1");
	}

	// 5. Test top-level router fallback_find_symbols with disk files
	{
		// Non-existent path returns cleanly without crashing
		std::vector<tools::codemap_symbol_info> missing_symbols;
		tools::fallback_find_symbols("/invalid/nonexistent/file_path_12345.cpp", 1, missing_symbols);
		assert(missing_symbols.empty());

		// Python file
		std::string test_py = "test_codemap_router_temp.py";
		{
			std::ofstream out(test_py);
			out << "def hello_world():\n    print('hi')\n";
		}
		std::vector<tools::codemap_symbol_info> py_file_symbols;
		tools::fallback_find_symbols(test_py, 1, py_file_symbols);
		std::filesystem::remove(test_py);
		assert(py_file_symbols.size() == 1);
		assert(py_file_symbols[0].name == "hello_world");

		// Markdown file
		std::string test_md = "test_codemap_router_temp.md";
		{
			std::ofstream out(test_md);
			out << "# Router Header\n\nSome body\n## Sub\n";
		}
		std::vector<tools::codemap_symbol_info> md_file_symbols;
		tools::fallback_find_symbols(test_md, 1, md_file_symbols);
		std::filesystem::remove(test_md);
		assert(md_file_symbols.size() == 2);
		assert(md_file_symbols[0].name == "Router Header");
		assert(md_file_symbols[1].name == "Sub");

		// Verilog file (.sv)
		std::string test_sv = "test_codemap_router_temp.sv";
		{
			std::ofstream out(test_sv);
			out << "module router_mod;\nendmodule\n";
		}
		std::vector<tools::codemap_symbol_info> sv_file_symbols;
		tools::fallback_find_symbols(test_sv, 1, sv_file_symbols);
		std::filesystem::remove(test_sv);
		assert(sv_file_symbols.size() == 1);
		assert(sv_file_symbols[0].name == "router_mod");

		// C++ file (.cpp)
		std::string test_cpp = "test_codemap_router_temp.cpp";
		{
			std::ofstream out(test_cpp);
			out << "int router_cpp_func()\n{\n    return 42;\n}\n";
		}
		std::vector<tools::codemap_symbol_info> cpp_file_symbols;
		tools::fallback_find_symbols(test_cpp, 1, cpp_file_symbols);
		std::filesystem::remove(test_cpp);
		assert(cpp_file_symbols.size() == 1);
		assert(cpp_file_symbols[0].name == "router_cpp_func");
	}

	// 5. Test empty inputs, parse_markdown_headings wrapper, and edge case coverage
	{
		std::vector<tools::codemap_symbol_info> empty_symbols;
		tools::fallback_find_symbols_cpp({}, empty_symbols);
		assert(empty_symbols.empty());

		tools::fallback_find_symbols_python({}, empty_symbols);
		assert(empty_symbols.empty());

		tools::fallback_find_symbols_verilog({}, empty_symbols);
		assert(empty_symbols.empty());

		tools::fallback_find_symbols_markdown({}, empty_symbols);
		assert(empty_symbols.empty());

		// Test parse_markdown_headings wrapper directly
		std::string md_text = "# Title\n#not_a_heading\n## Section\n";
		std::vector<tools::codemap_symbol_info> md_parsed;
		tools::parse_markdown_headings(md_text, 1, md_parsed);
		assert(md_parsed.size() == 2);
		assert(md_parsed[0].name == "Title");
		assert(md_parsed[1].name == "Section");

		// Test C++ function with struct parameter (close paren before open brace)
		// and backward walk encountering non-specifier tokens
		std::vector<std::string> edge_cpp = {
		    "int some_var = 42;",
		    "void test_param(struct Data d) {",
		    "    return;",
		    "}",
		};
		std::vector<tools::codemap_symbol_info> edge_symbols;
		tools::fallback_find_symbols_cpp(edge_cpp, edge_symbols);
		assert(edge_symbols.size() == 1);
		assert(edge_symbols[0].name == "test_param");
	}

	std::cout << "All codemap_fallbacks unit tests passed successfully!" << std::endl;
	return 0;
}
