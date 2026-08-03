#include "test_watchdog.h"
#include "agentlib/tool_context.h"
#include "agentlib/tool_registry.h"
#include "tools/codemap_utils.h"
#include "lsp_manager.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <string>

int main()
{
	test_watchdog::setup_watchdog(30);

	agentlib::tool_registry &registry = agentlib::tool_registry::get_instance();
	agentlib::tool_context ctx;
	ctx.fs_security.set_working_directory(std::filesystem::current_path());
	ctx.fs_security.add_allowed_root(std::filesystem::current_path(), agentlib::access_type::read);

	// 1. Create sample implementation file test_sample_impl.cpp (40 lines so partial reads don't auto-expand to EOF)
	std::string impl_file = "test_sample_impl.cpp";
	std::ofstream out_impl(impl_file);
	out_impl << "#include \"test_sample_impl.h\"\n\n"
		 << "void sample_foo()\n"
		 << "{\n"
		 << "    int a = 1;\n"
		 << "}\n\n"
		 << "int sample_bar(int x)\n"
		 << "{\n"
		 << "    return x + 42;\n"
		 << "}\n\n";
	for (int i = 0; i < 30; ++i) {
		out_impl << "// Padding line " << i << "\n";
	}
	out_impl.close();

	// 2. Create sample header file test_sample_impl.h
	std::string header_file = "test_sample_impl.h";
	std::ofstream out_hdr(header_file);
	out_hdr << "#pragma once\n\n"
		<< "void sample_foo();\n"
		<< "int sample_bar(int x);\n";
	out_hdr.close();

	// 3. Test fs_file_codemap standalone tool (Rich format)
	nlohmann::json codemap_args = {{"path", impl_file}};
	std::string codemap_res = registry.execute_tool("fs_file_codemap", codemap_args.dump(), ctx);
	std::cout << "fs_file_codemap output:\n" << codemap_res << "\n";
	assert(codemap_res.find("Codemap for `test_sample_impl.cpp`") != std::string::npos);
	assert(codemap_res.find("| Symbol | Kind | Start Line | End Line | Lines |") != std::string::npos);
	assert(codemap_res.find("`sample_foo`") != std::string::npos);
	assert(codemap_res.find("`sample_bar`") != std::string::npos);

	// 4. Test fs_read_lines partial read (Compact 3-column format)
	nlohmann::json read_partial_args = {{"path", impl_file}, {"start_line", 1}, {"end_line", 5}};
	std::string read_partial_res = registry.execute_tool("fs_read_lines", read_partial_args.dump(), ctx);
	std::cout << "fs_read_lines partial read output:\n" << read_partial_res << "\n";
	assert(read_partial_res.find("| Function | Start Line | End Line |") != std::string::npos);
	assert(read_partial_res.find("`sample_foo`") != std::string::npos);

	// 5. Test fs_read_lines full read (SKIP table rule)
	nlohmann::json read_full_args = {{"path", impl_file}, {"start_line", 1}, {"end_line", 100}};
	std::string read_full_res = registry.execute_tool("fs_read_lines", read_full_args.dump(), ctx);
	std::cout << "fs_read_lines full read output:\n" << read_full_res << "\n";
	assert(read_full_res.find("| Function | Start Line | End Line |") == std::string::npos);

	// 6. Test fs_read_lines on header file (Header -> Impl magic)
	nlohmann::json read_hdr_args = {{"path", header_file}, {"start_line", 1}, {"end_line", 4}};
	std::string read_hdr_res = registry.execute_tool("fs_read_lines", read_hdr_args.dump(), ctx);
	std::cout << "fs_read_lines header output:\n" << read_hdr_res << "\n";
	assert(read_hdr_res.find("Codemap for `test_sample_impl.cpp`") != std::string::npos);
	assert(read_hdr_res.find("`sample_foo`") != std::string::npos);

	// 7. Test min_lines filter parameter
	nlohmann::json min_lines_args = {{"path", impl_file}, {"min_lines", 100}};
	std::string min_lines_res = registry.execute_tool("fs_file_codemap", min_lines_args.dump(), ctx);
	std::cout << "fs_file_codemap min_lines=100 output:\n" << min_lines_res << "\n";
	assert(min_lines_res.find("No functions, classes, or symbols found") != std::string::npos);

	// 8. Test Option B scope indentation for qualified C++ method definitions (Class::method)
	std::string class_file = "test_class_impl.cpp";
	std::ofstream out_class(class_file);
	out_class << "void my_class::foo()\n{\n    int a = 1;\n}\n\n"
		  << "void my_class::bar()\n{\n    int b = 2;\n}\n";
	out_class.close();

	nlohmann::json class_args = {{"path", class_file}};
	std::string class_res = registry.execute_tool("fs_file_codemap", class_args.dump(), ctx);
	std::cout << "fs_file_codemap class methods output:\n" << class_res << "\n";
	assert(class_res.find("`my_class`") != std::string::npos);
	assert(class_res.find("`    ::foo`") != std::string::npos);
	assert(class_res.find("`    ::bar`") != std::string::npos);
	std::remove(class_file.c_str());

	// 9. Test LSP symbol caching (Second call retrieves cached results)
	auto symbols1 = tools::get_document_codemap_symbols(impl_file, ctx, 1);
	auto symbols2 = tools::get_document_codemap_symbols(impl_file, ctx, 1);
	assert(!symbols1.empty());
	assert(symbols1.size() == symbols2.size());

	// Cleanup
	std::remove(impl_file.c_str());
	std::remove(header_file.c_str());

	std::cout << "test_fs_file_codemap passed successfully!\n";
	return 0;
}
