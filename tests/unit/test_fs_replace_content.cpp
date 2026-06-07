#include <cassert>
#include <iostream>
#include <fstream>
#include <filesystem>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/project_manager.h"

using namespace agentlib;

int main()
{
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
			std::ofstream out(temp_file, std::ios::binary);
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

		// 6. Security check: reject ".." directory traversal
		{
			std::string args = "{\"path\": \"../tmp/escaped.txt\", \"target_content\": \"foo\", \"replacement_content\": \"bar\"}";
			auto prep = registry.prepare_tool("fs_replace_content", args, ctx);
			assert(prep.tool == nullptr);
			assert(!prep.error_message.empty());
			assert(prep.error_message.find("cannot contain '..' directory traversal") != std::string::npos);
		}

		// Clean up
		std::filesystem::remove(temp_file);
		std::cout << "fs_replace_content tool verified successfully!" << std::endl;
	}

	return 0;
}
