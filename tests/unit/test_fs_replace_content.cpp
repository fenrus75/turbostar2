#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include <filesystem>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/agentlib/virtual_file_system.h"
#include "../../src/project_manager.h"
#include "../../src/fs_utils.h"

using namespace agentlib;

int main()
{
	test_watchdog::setup_watchdog(30);
	project_manager::get_instance().initialize();

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;

	std::string project_root = project_manager::get_instance().get_project_root();
	ctx.fs_security.set_working_directory(project_root);
	ctx.fs_security.add_allowed_root(project_root, access_type::read);
	ctx.fs_security.add_allowed_root(project_root, access_type::write);

	auto model = std::make_shared<ai_model>("test-model", "Test Model", "http://localhost", "Test", 0.0, 0.0);
	auto agent = ai_agent::create(1, "TestAgent", model, nullptr, nullptr);
	ctx.active_agent = agent.get();

	// Setup a temporary file for editing
	std::string temp_file = project_root + "/tmp/test_replace_content.txt";
	std::filesystem::create_directories(project_root + "/tmp");

	std::cout << "Testing fs_replace_content..." << std::endl;
	{
		// Helper to write content
		auto write_file = [&](const std::string& content) {
			std::ofstream out(temp_file, std::ios::binary | std::ios::trunc);
			out.write(content.data(), content.length());
			out.close();
		};

		// Helper to read content
		auto read_file = [&]() {
			std::ifstream in(temp_file, std::ios::binary);
			std::stringstream buffer;
			buffer << in.rdbuf();
			return buffer.str();
		};

		// 1. Success case: unique replacement
		{
			write_file("line 1\nline 2\ntarget block here\nline 4\n");
			std::string args = "{\"path\": \"" + temp_file + "\", \"target_content\": \"target block here\", \"replacement_content\": \"substituted block\"}";
			std::string res = registry.execute_tool("fs_replace_content", args, ctx);
			std::cout << "Unique replacement result: " << res << std::endl;
			assert(res.find("Successfully replaced") != std::string::npos);
			assert(read_file() == "line 1\nline 2\nsubstituted block\nline 4\n");
		}

		// 2. Failure case: target_content not found
		{
			write_file("line 1\nline 2\n");
			std::string args = "{\"path\": \"" + temp_file + "\", \"target_content\": \"missing block\", \"replacement_content\": \"substituted\"}";
			std::string res = registry.execute_tool("fs_replace_content", args, ctx);
			std::cout << "Missing block result: " << res << std::endl;
			assert(res.find("Error: target_content not found") != std::string::npos);
		}

		// 3. Ambiguous failure case: multiple matches, no line hint
		{
			write_file("target block\nline 2\ntarget block\nline 4\n");
			std::string args = "{\"path\": \"" + temp_file + "\", \"target_content\": \"target block\", \"replacement_content\": \"substituted\"}";
			std::string res = registry.execute_tool("fs_replace_content", args, ctx);
			std::cout << "Multiple matches no hint result: " << res << std::endl;
			assert(res.find("Error: Multiple matches (2)") != std::string::npos);
			assert(res.find("line numbers: [1, 3]") != std::string::npos);
			// content should remain unchanged
			assert(read_file() == "target block\nline 2\ntarget block\nline 4\n");
		}

		// 4. Ambiguous success case: multiple matches, closest to line hint wins (closest to 1)
		{
			write_file("target block\nline 2\ntarget block\nline 4\n");
			std::string args = "{\"path\": \"" + temp_file + "\", \"target_content\": \"target block\", \"replacement_content\": \"substituted\", \"line_hint\": 1}";
			std::string res = registry.execute_tool("fs_replace_content", args, ctx);
			std::cout << "Multiple matches hint=1 result: " << res << std::endl;
			assert(res.find("starting at line 1") != std::string::npos);
			assert(read_file() == "substituted\nline 2\ntarget block\nline 4\n");
		}

		// 5. Ambiguous success case: multiple matches, closest to line hint wins (closest to 3)
		{
			write_file("target block\nline 2\ntarget block\nline 4\n");
			std::string args = "{\"path\": \"" + temp_file + "\", \"target_content\": \"target block\", \"replacement_content\": \"substituted\", \"line_hint\": 3}";
			std::string res = registry.execute_tool("fs_replace_content", args, ctx);
			std::cout << "Multiple matches hint=3 result: " << res << std::endl;
			assert(res.find("starting at line 3") != std::string::npos);
			assert(read_file() == "target block\nline 2\nsubstituted\nline 4\n");
		}

		// 6. Security check: reject directory traversal
		{
			std::string args = "{\"path\": \"../tmp/escaped.txt\", \"target_content\": \"foo\", \"replacement_content\": \"bar\"}";
			auto prep = registry.prepare_tool("fs_replace_content", args, ctx);
			assert(prep.tool == nullptr);
			assert(!prep.error_message.empty());
		}

		// 7. VFS Success case: replacement in a local VFS file (tmp://)
		{
			auto global_vfs = std::make_shared<virtual_file_system>();
			auto tmp_prov = std::make_shared<file_vfs_provider>("tmp", fs_utils::get_project_tmp_dir());
			global_vfs->register_provider("tmp", tmp_prov);
			ctx.fs_security.set_vfs(global_vfs.get());

			// Write standard file via local system under tmp dir
			std::string physical_tmp_file = fs_utils::get_project_tmp_dir() + "/test_replace_vfs.txt";
			std::ofstream out(physical_tmp_file);
			out << "line 1\ntarget block\nline 3\n";
			out.close();

			std::string args = "{\"path\": \"tmp://test_replace_vfs.txt\", \"target_content\": \"target block\", \"replacement_content\": \"substituted block\"}";
			std::string res = registry.execute_tool("fs_replace_content", args, ctx);
			std::cout << "VFS replacement result: " << res << std::endl;
			assert(res.find("Successfully replaced") != std::string::npos);

			// Read back physical file to verify
			std::ifstream in(physical_tmp_file, std::ios::binary);
			std::stringstream buffer;
			buffer << in.rdbuf();
			assert(buffer.str() == "line 1\nsubstituted block\nline 3\n");
			in.close();

			std::filesystem::remove(physical_tmp_file);
			ctx.fs_security.set_vfs(nullptr);
		}

		// 8. function_hint disambiguation case
		{
			write_file("void func_one() {\n    return;\n}\n\nvoid func_two() {\n    return;\n}\n");
			nlohmann::json json_args = {
				{"path", temp_file},
				{"target_content", "return;"},
				{"replacement_content", "return_two();"},
				{"function_hint", "func_two"}
			};
			std::string res = registry.execute_tool("fs_replace_content", json_args.dump(), ctx);
			std::cout << "function_hint result: " << res << std::endl;
			assert(res.find("Successfully replaced") != std::string::npos);
			assert(read_file() == "void func_one() {\n    return;\n}\n\nvoid func_two() {\n    return_two();\n}\n");
		}

		// 9. Tab/space relaxed matching case (hard tabs in file, spaces in target)
		{
			write_file("void test_tabs() {\n\t\tdo_work();\n}\n");
			nlohmann::json json_args = {
				{"path", temp_file},
				{"target_content", "        do_work();"},
				{"replacement_content", "        do_work_new();"}
			};
			std::string res = registry.execute_tool("fs_replace_content", json_args.dump(), ctx);
			std::cout << "Tab/space relaxed result: " << res << std::endl;
			std::cout << "Actual read_file(): [" << read_file() << "]" << std::endl;
			assert(res.find("Successfully replaced") != std::string::npos);
			assert(read_file() == "void test_tabs() {\n        do_work_new();\n}\n");
		}

		// 10. Multi-line leading whitespace relaxed matching case
		{
			write_file("void multi_line() {\n    if (cond) {\n        execute();\n    }\n}\n");
			nlohmann::json json_args = {
				{"path", temp_file},
				{"target_content", "if (cond) {\n  execute();\n}"},
				{"replacement_content", "if (cond) {\n        execute_new();\n    }"}
			};
			std::string res = registry.execute_tool("fs_replace_content", json_args.dump(), ctx);
			std::cout << "Multi-line relaxed result: " << res << std::endl;
			assert(res.find("Successfully replaced") != std::string::npos);
			assert(read_file() == "void multi_line() {\n    if (cond) {\n        execute_new();\n    }\n}\n");
		}

		// 11. Ambiguous failure case with function_hint provided (Combo 2)
		{
			write_file("void double_return() {\n    return;\n    return;\n}\n");
			nlohmann::json json_args = {
				{"path", temp_file},
				{"target_content", "return;"},
				{"replacement_content", "return_two();"},
				{"function_hint", "double_return"}
			};
			std::string res = registry.execute_tool("fs_replace_content", json_args.dump(), ctx);
			std::cout << "function_hint combo 2 result: " << res << std::endl;
			assert(res.find("Multiple occurrences exist even within function/scope 'double_return'") != std::string::npos);
			assert(res.find("Please pass 'line_hint'") != std::string::npos);
		}

		// 12. Level D relaxed matching case (1-line target with leading space mismatch)
		{
			write_file("void level_d_test() {\n    do_single_work();\n}\n");
			nlohmann::json json_args = {
				{"path", temp_file},
				{"target_content", "  do_single_work();"}, // 2 spaces target vs 4 spaces file
				{"replacement_content", "    do_single_work_updated();"}
			};
			std::string res = registry.execute_tool("fs_replace_content", json_args.dump(), ctx);
			std::cout << "Level D relaxed result: " << res << std::endl;
			assert(res.find("Successfully replaced") != std::string::npos);
			assert(read_file() == "void level_d_test() {\n    do_single_work_updated();\n}\n");
		}

		// 13. Brace-balance warning case: replacement removes a closing brace.
		// .txt files have no LSP function symbols, so the whole-file fallback runs.
		{
			write_file("void foo() {\n    x();\n}\n");
			// Replace the closing brace line with empty text leaving an unbalanced brace.
			nlohmann::json json_args = {
				{"path", temp_file},
				{"target_content", "}"},
				{"replacement_content", "    // removed closing brace"}
			};
			std::string res = registry.execute_tool("fs_replace_content", json_args.dump(), ctx);
			std::cout << "Brace-balance warning result: " << res << std::endl;
			assert(res.find("Successfully replaced") != std::string::npos);
			// The edit still applies (warning, not failure), but the result must flag the imbalance.
			assert(res.find("unbalanced braces") != std::string::npos);
			assert(read_file() == "void foo() {\n    x();\n    // removed closing brace\n");
		}

		// 14. Brace-balance clean case: balanced replacement produces no warning.
		{
			write_file("void bar() {\n    y();\n}\n");
			nlohmann::json json_args = {
				{"path", temp_file},
				{"target_content", "    y();"},
				{"replacement_content", "    y_updated();"}
			};
			std::string res = registry.execute_tool("fs_replace_content", json_args.dump(), ctx);
			std::cout << "Brace-balance clean result: " << res << std::endl;
			assert(res.find("Successfully replaced") != std::string::npos);
			assert(res.find("unbalanced braces") == std::string::npos);
			assert(read_file() == "void bar() {\n    y_updated();\n}\n");
		}

		// 15. Strict mode: unbalanced-brace replacement must be rejected and file left unchanged.
		{
			write_file("void strict_foo() {\n    x();\n}\n");
			nlohmann::json json_args = {
				{"path", temp_file},
				{"target_content", "}"},
				{"replacement_content", "    // removed closing brace"},
				{"strict", true}
			};
			std::string res = registry.execute_tool("fs_replace_content", json_args.dump(), ctx);
			std::cout << "Strict brace result: " << res << std::endl;
			assert(res.find("rejected (strict mode)") != std::string::npos);
			assert(res.find("not applied") != std::string::npos);
			// File must be unchanged.
			assert(read_file() == "void strict_foo() {\n    x();\n}\n");
		}

		// 16. Windowed replacement with start_line and end_line
		{
			write_file("line 1\nTarget block\nline 3\nline 4\nline 5\nline 6\nline 7\nline 8\nline 9\nTarget block\nline 11\n");
			nlohmann::json json_args = {
				{"path", temp_file},
				{"target_content", "Target block"},
				{"replacement_content", "Windowed block replacement"},
				{"start_line", 8},
				{"end_line", 11}
			};
			std::string res = registry.execute_tool("fs_replace_content", json_args.dump(), ctx);
			std::cout << "Windowed search result: " << res << std::endl;
			assert(res.find("Successfully replaced") != std::string::npos);
			assert(read_file() == "line 1\nTarget block\nline 3\nline 4\nline 5\nline 6\nline 7\nline 8\nline 9\nWindowed block replacement\nline 11\n");
		}

		// Clean up
		std::filesystem::remove(temp_file);
		std::cout << "fs_replace_content tool verified successfully!" << std::endl;
	}

	return 0;
}
