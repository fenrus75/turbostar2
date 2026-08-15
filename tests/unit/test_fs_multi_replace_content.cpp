#include "test_watchdog.h"
#include "../../src/agentlib/tool_context.h"
#include "../../src/tools/fs_replace_content/fs_multi_replace_content.h"
#include "../../src/tools/fs_replace_content/fs_replace_content.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

int main()
{
	test_watchdog::setup_watchdog(30);

	fs::path temp_dir = fs::temp_directory_path() / "turbostar_test_fs_multi_replace";
	fs::create_directories(temp_dir);

	fs::path test_file = temp_dir / "sample.cpp";

	std::string original_code =
		"#include <iostream>\n"
		"\n"
		"void func1() {\n"
		"    std::cout << \"Hello from func1\" << std::endl;\n"
		"}\n"
		"\n"
		"void func2() {\n"
		"    std::cout << \"Hello from func2\" << std::endl;\n"
		"}\n"
		"\n"
		"int main() {\n"
		"    func1();\n"
		"    func2();\n"
		"    return 0;\n"
		"}\n";

	{
		std::ofstream out(test_file);
		out << original_code;
	}

	agentlib::tool_context ctx;
	std::string root_str = temp_dir.string();
	ctx.fs_security.set_working_directory(root_str);
	ctx.fs_security.add_allowed_root(root_str, agentlib::access_type::read);
	ctx.fs_security.add_allowed_root(root_str, agentlib::access_type::write);

	// Test 1: Successful multi-chunk replacement with function_scope and explicit bounds
	{
		tools::fs_multi_replace_content_args args;
		args.path = "sample.cpp";
		args.safe_path = test_file.string();

		tools::replace_chunk chunk1;
		chunk1.function_scope = "func1";
		chunk1.target_content = "std::cout << \"Hello from func1\" << std::endl;";
		chunk1.replacement_content = "std::cout << \"UPDATED FUNC1\" << std::endl;";

		tools::replace_chunk chunk2;
		chunk2.start_line = 6;
		chunk2.end_line = 10;
		chunk2.target_content = "std::cout << \"Hello from func2\" << std::endl;";
		chunk2.replacement_content = "std::cout << \"UPDATED FUNC2\" << std::endl;";

		args.chunks.push_back(chunk1);
		args.chunks.push_back(chunk2);

		tools::fs_multi_replace_content_tool tool(args);
		assert(tool.validate_runtime(ctx, args.path));

		std::string result = tool.execute(ctx);
		assert(result.find("Successfully applied 2 chunk replacements") != std::string::npos);

		std::ifstream in(test_file);
		std::string updated((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		assert(updated.find("UPDATED FUNC1") != std::string::npos);
		assert(updated.find("UPDATED FUNC2") != std::string::npos);
	}

	// Test 2: Atomic transactional rollback when one chunk in a batch fails to match
	{
		std::string before_failed_attempt;
		{
			std::ifstream in(test_file);
			before_failed_attempt.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		}

		tools::fs_multi_replace_content_args args;
		args.path = "sample.cpp";
		args.safe_path = test_file.string();

		tools::replace_chunk chunk1;
		chunk1.target_content = "UPDATED FUNC1";
		chunk1.replacement_content = "NEW FUNC1";

		tools::replace_chunk chunk2;
		chunk2.target_content = "NON_EXISTENT_TEXT_THAT_WILL_FAIL_MATCHING";
		chunk2.replacement_content = "FAIL";

		args.chunks.push_back(chunk1);
		args.chunks.push_back(chunk2);

		tools::fs_multi_replace_content_tool tool(args);
		std::string result = tool.execute(ctx);

		assert(result.find("Error: Could not locate target_content for chunk 2") != std::string::npos);

		// Verify atomic rollback (file is 100% unchanged)
		std::ifstream in(test_file);
		std::string after_failed_attempt((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		assert(after_failed_attempt == before_failed_attempt);
		assert(after_failed_attempt.find("NEW FUNC1") == std::string::npos);
	}

	// Test 3: Strict mode brace balance rejection
	{
		tools::fs_multi_replace_content_args args;
		args.path = "sample.cpp";
		args.safe_path = test_file.string();
		args.strict = true;

		tools::replace_chunk chunk1;
		chunk1.function_scope = "func1";
		chunk1.target_content = "void func1() {";
		chunk1.replacement_content = "void func1() { { {"; // Breaks brace balance!

		args.chunks.push_back(chunk1);

		tools::fs_multi_replace_content_tool tool(args);
		std::string result = tool.execute(ctx);

		assert(result.find("Error: Edit rejected (strict mode)") != std::string::npos);
		assert(result.find("unbalanced braces") != std::string::npos);
	}

	// Cleanup
	fs::remove_all(temp_dir);

	std::cout << "All fs_multi_replace_content unit tests passed!" << std::endl;
	return 0;
}
