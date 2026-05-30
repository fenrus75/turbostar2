#include <cassert>
#include <iostream>
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

	std::cout << "Testing fs_regexp_lines..." << std::endl;
	{
		std::string poem_path = "tests/unit/poem.txt";

		// 1. Success case: find matches for "sun"
		{
			std::string args = "{\"path\": \"" + poem_path + "\", \"pattern\": \"sun.*\"}";
			std::string res = registry.execute_tool("fs_regexp_lines", args, ctx);
			std::cout << "Regex search result: " << res << std::endl;
			assert(res.find("| Line Number | Content |") != std::string::npos);
			assert(res.find("shines") != std::string::npos);
		}

		// 2. Stage 2 validation failure: invalid RE2 regex pattern (e.g. unmatched parenthesis)
		{
			std::string args = "{\"path\": \"" + poem_path + "\", \"pattern\": \"(unmatched\"}";
			auto prep = registry.prepare_tool("fs_regexp_lines", args, ctx);
			assert(prep.tool == nullptr);
			assert(!prep.error_message.empty());
			assert(prep.error_message.find("Invalid regular expression") != std::string::npos);
		}

		// 3. Stage 1 validation failure: reject empty path
		{
			auto prep = registry.prepare_tool("fs_regexp_lines", "{\"path\": \"\", \"pattern\": \"sun\"}", ctx);
			assert(prep.tool == nullptr);
			assert(!prep.error_message.empty());
		}

		// 4. Stage 1 validation failure: reject empty pattern
		{
			auto prep = registry.prepare_tool("fs_regexp_lines", "{\"path\": \"tests/unit/poem.txt\", \"pattern\": \"\"}", ctx);
			assert(prep.tool == nullptr);
			assert(!prep.error_message.empty());
		}

		// 5. Stage 1 validation failure: reject unexpected properties (based on review recommendations)
		{
			std::string args = "{\"path\": \"" + poem_path + "\", \"pattern\": \"sun\", \"unexpected_arg\": 123}";
			auto prep = registry.prepare_tool("fs_regexp_lines", args, ctx);
			assert(prep.tool == nullptr); // This will fail initially as expected
			assert(!prep.error_message.empty());
		}

		// 6. Stage 2 validation failure: reject path outside workspace
		{
			auto prep = registry.prepare_tool("fs_regexp_lines", "{\"path\": \"/etc/passwd\", \"pattern\": \"root\"}", ctx);
			assert(prep.tool == nullptr);
			assert(!prep.error_message.empty());
		}

		std::cout << "fs_regexp_lines tool verified successfully!" << std::endl;
	}

	return 0;
}
