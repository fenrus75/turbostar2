// Tested source file: src/tools/fs_write_file/fs_write_file_entry.cpp, src/languages/balance_checker.cpp
#include "agentlib/tool_context.h"
#include "tools/fs_write_file/fs_write_file.h"
#include "test_watchdog.h"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <unistd.h>

namespace fs = std::filesystem;

static std::string run_write(agentlib::tool_context &ctx, const fs::path &rel_path, const std::string &content, bool append = false)
{
	tools::fs_write_file_args args;
	args.path = rel_path.string();
	args.safe_path = (ctx.fs_security.get_working_directory() / rel_path).string();
	args.content = content;
	args.append = append;
	tools::fs_write_file_tool tool(args);
	return tool.execute(ctx);
}

int main()
{
	test_watchdog::setup_watchdog(30);

	fs::path tmp_root = fs::temp_directory_path() / ("test_fs_write_file_" + std::to_string(getpid()));
	fs::remove_all(tmp_root);
	fs::create_directories(tmp_root);

	agentlib::tool_context ctx;
	ctx.fs_security.set_working_directory(tmp_root.string());

	// 1. Balanced C++ file -> no warning
	std::string res_cpp_ok = run_write(ctx, "ok.cpp", "int main() {\n\treturn 0;\n}\n");
	assert(res_cpp_ok.find("Successfully wrote") != std::string::npos);
	assert(res_cpp_ok.find("Warning:") == std::string::npos);

	// 2. Unbalanced C++ file -> brace warning
	std::string res_cpp_bad = run_write(ctx, "bad.cpp", "int main() {\n\tif (true) {\n\t\treturn 0;\n\t}\n");
	assert(res_cpp_bad.find("unbalanced braces") != std::string::npos);
	assert(res_cpp_bad.find("missing 1 closing '}'") != std::string::npos);

	// 3. Append closing brace -> warning clears on resulting file
	std::string res_cpp_fixed = run_write(ctx, "bad.cpp", "}\n", /*append=*/true);
	assert(res_cpp_fixed.find("Successfully appended") != std::string::npos);
	assert(res_cpp_fixed.find("Warning:") == std::string::npos);

	// 4. Balanced SystemVerilog file with sized single-quote literals -> no warning
	std::string sv_ok =
	    "module alu(input logic clk, output logic [7:0] y);\n"
	    "    always_ff @(posedge clk) begin\n"
	    "        y <= 8'hFF;\n"
	    "    end\n"
	    "endmodule\n";
	std::string res_sv_ok = run_write(ctx, "alu.sv", sv_ok);
	assert(res_sv_ok.find("Warning:") == std::string::npos);

	// 5. Unbalanced SystemVerilog file (missing 'end') -> begin/end warning
	std::string sv_bad =
	    "module alu(input logic clk, output logic [7:0] y);\n"
	    "    always_ff @(posedge clk) begin\n"
	    "        y <= 8'hFF;\n"
	    "endmodule\n";
	std::string res_sv_bad = run_write(ctx, "alu_bad.sv", sv_bad);
	assert(res_sv_bad.find("unbalanced begin/end") != std::string::npos);
	assert(res_sv_bad.find("missing 1 closing 'end'") != std::string::npos);

	// 6. Prose Markdown file with stray '{' -> no false-positive warning
	std::string res_md = run_write(ctx, "notes.md", "# Notes\nUse `{placeholder` in docs.\n");
	assert(res_md.find("Warning:") == std::string::npos);

	fs::remove_all(tmp_root);
	std::cout << "PASS: test_fs_write_file\n";
	return 0;
}
