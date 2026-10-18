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

		std::vector<tools::codemap_symbol_info> py_symbols;
		tools::fallback_find_symbols_python(py_lines, 1, py_symbols);

		assert(py_symbols.size() == 3);
		assert(py_symbols[0].name == "Worker");
		assert(py_symbols[0].start_line == 4);
		assert(py_symbols[1].name == "__init__");
		assert(py_symbols[1].start_line == 5);
		assert(py_symbols[2].name == "run_job");
		assert(py_symbols[2].start_line == 8);
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
		};

		std::vector<tools::codemap_symbol_info> sv_symbols;
		tools::fallback_find_symbols_verilog(sv_lines, 1, sv_symbols);

		assert(sv_symbols.size() == 3);
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
	}

	// 3. Test C / C++ fallback parser
	{
		std::vector<std::string> cpp_lines = {
		    "// C++ Sample",
		    "#include <vector>",
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
		    "// Forward declaration - should NOT be recognized as full function",
		    "void forward_decl(int a);",
		    "",
		    "static inline int compute_sum(int a, int b)",
		    "{",
		    "    return a + b;",
		    "}",
		};

		std::vector<tools::codemap_symbol_info> cpp_symbols;
		tools::fallback_find_symbols_cpp(cpp_lines, 1, cpp_symbols);

		assert(cpp_symbols.size() == 3);
		assert(cpp_symbols[0].name == "Point");
		assert(cpp_symbols[0].kind_str == "Struct");
		assert(cpp_symbols[0].start_line == 4);
		assert(cpp_symbols[0].end_line == 7);

		assert(cpp_symbols[1].name == "Status");
		assert(cpp_symbols[1].kind_str == "Enum");
		assert(cpp_symbols[1].start_line == 9);
		assert(cpp_symbols[1].end_line == 12);

		assert(cpp_symbols[2].name == "compute_sum");
		assert(cpp_symbols[2].kind_str == "Function");
		assert(cpp_symbols[2].start_line == 17);
		assert(cpp_symbols[2].end_line == 20);
	}

	// 4. Test Markdown headings parser (both span lines and string wrapper)
	{
		std::vector<std::string> md_lines = {
		    "# Title",
		    "",
		    "Intro text.",
		    "",
		    "## Section 1",
		    "Details",
		    "",
		    "### Subsection 1.1",
		    "More details",
		    "",
		    "## Section 2",
		    "Closing",
		};

		std::vector<tools::codemap_symbol_info> md_symbols;
		tools::fallback_find_symbols_markdown(md_lines, 1, md_symbols);

		assert(md_symbols.size() == 4);
		assert(md_symbols[0].name == "Title");
		assert(md_symbols[0].depth == 0);
		assert(md_symbols[0].start_line == 1);

		assert(md_symbols[1].name == "Section 1");
		assert(md_symbols[1].depth == 1);
		assert(md_symbols[1].start_line == 5);

		assert(md_symbols[2].name == "Subsection 1.1");
		assert(md_symbols[2].depth == 2);
		assert(md_symbols[2].start_line == 8);

		assert(md_symbols[3].name == "Section 2");
		assert(md_symbols[3].depth == 1);
		assert(md_symbols[3].start_line == 11);

		// Also verify backward-compatible parse_markdown_headings
		std::string md_doc = "# Title\n\nIntro text.\n\n## Section 1\nDetails\n\n### Subsection 1.1\nMore details\n\n## Section 2\nClosing";
		std::vector<tools::codemap_symbol_info> wrapper_symbols;
		tools::parse_markdown_headings(md_doc, 1, wrapper_symbols);
		assert(wrapper_symbols.size() == 4);
		assert(wrapper_symbols[0].name == "Title");
	}

	// 5. Test top-level router fallback_find_symbols with disk files (Python & Markdown)
	{
		std::string test_py = "test_codemap_router_temp.py";
		{
			std::ofstream out(test_py);
			out << "def hello_world():\n    print('hi')\n";
		}

		std::vector<tools::codemap_symbol_info> symbols;
		tools::fallback_find_symbols(test_py, 1, symbols);
		std::filesystem::remove(test_py);

		assert(symbols.size() == 1);
		assert(symbols[0].name == "hello_world");
		assert(symbols[0].start_line == 1);

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
	}

	std::cout << "All codemap_fallbacks unit tests passed successfully!" << std::endl;
	return 0;
}
