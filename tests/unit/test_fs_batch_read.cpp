// Tested source file: src/tools/fs_read_lines/fs_batch_read_entry.cpp
#include <cassert>
#include <iostream>
#include <nlohmann/json.hpp>
#include "agentlib/ai_agent.h"
#include "agentlib/tool_registry.h"
#include "fs_utils.h"
#include "project_manager.h"
#include "test_watchdog.h"

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

	std::cout << "Testing fs_batch_read..." << std::endl;

	std::string poem_path = "tests/unit/poem.txt";

	// Test 1: Multi-item batch reading lines
	{
		std::cout << "1. Multi-item batch reading lines..." << std::endl;
		nlohmann::json args = {{"items",
					{{{"path", poem_path}, {"type", "lines"}, {"start", 1}, {"end", 2}},
					 {{"path", poem_path}, {"type", "lines"}, {"start", 3}, {"end", 4}}}}};

		std::string raw_res = registry.execute_tool("fs_batch_read", args.dump(), ctx);
		std::string res = fs_utils::unwrap_prompt_untrusted_data_tag(raw_res);

		assert(res.find("### [1/2]") != std::string::npos);
		assert(res.find("### [2/2]") != std::string::npos);
		assert(res.find("1:") != std::string::npos);
		assert(res.find("3:") != std::string::npos);
		// Suppressed codemap in batch mode (>1 items)
		assert(res.find("### Codemap") == std::string::npos);
	}

	// Test 2: Single-item batch delegates to fs_read_lines behavior
	{
		std::cout << "2. Single-item batch..." << std::endl;
		nlohmann::json args = {{"items", {{{"path", poem_path}, {"type", "lines"}, {"start", 1}, {"end", 3}}}}};

		std::string raw_res = registry.execute_tool("fs_batch_read", args.dump(), ctx);
		std::string res = fs_utils::unwrap_prompt_untrusted_data_tag(raw_res);

		std::cout << "Test 2 res:\n" << res << "\n";
		assert(res.find("### [1/1]") == std::string::npos);
		assert(res.find("Code for lines 1 - ") != std::string::npos);
		assert(res.find("tests/unit/poem.txt") != std::string::npos);
		assert(res.find("1:") != std::string::npos);
	}

	// Test 3: Batch reading with tail and length
	{
		std::cout << "3. Batch reading with tail and length..." << std::endl;
		nlohmann::json args = {{"items", {{{"path", poem_path}, {"start", 1}, {"length", 2}}, {{"path", poem_path}, {"tail", 2}}}}};

		std::string raw_res = registry.execute_tool("fs_batch_read", args.dump(), ctx);
		std::string res = fs_utils::unwrap_prompt_untrusted_data_tag(raw_res);

		assert(res.find("### [1/2]") != std::string::npos);
		assert(res.find("### [2/2]") != std::string::npos);
		assert(res.find("(tail 2)") != std::string::npos);
	}

	// Test 4: Resilience - partial failure with inline error notes
	{
		std::cout << "4. Partial failure resilience..." << std::endl;
		nlohmann::json args = {{"items",
					{{{"path", poem_path}, {"start", 1}, {"end", 2}},
					 {{"path", "tests/unit/non_existent_file_98765.txt"}, {"start", 1}, {"end", 5}},
					 {{"path", poem_path}, {"start", 3}, {"end", 4}}}}};

		std::string raw_res = registry.execute_tool("fs_batch_read", args.dump(), ctx);
		std::string res = fs_utils::unwrap_prompt_untrusted_data_tag(raw_res);

		assert(res.find("### [1/3]") != std::string::npos);
		assert(res.find("### [2/3]") != std::string::npos);
		assert(res.find("### [3/3]") != std::string::npos);

		// Item 1 succeeded
		assert(res.find("1:") != std::string::npos);
		// Item 2 failed with inline error note
		assert(res.find("Error: File does not exist") != std::string::npos);
		// Item 3 succeeded
		assert(res.find("3:") != std::string::npos);
	}

	// Test 5: Batch reading symbol
	{
		std::cout << "5. Batch reading symbol..." << std::endl;
		nlohmann::json args = {{"items", {{{"path", "src/mime.cpp"}, {"type", "symbol"}, {"name", "from_extension"}}}}};

		std::string raw_res = registry.execute_tool("fs_batch_read", args.dump(), ctx);
		std::string res = fs_utils::unwrap_prompt_untrusted_data_tag(raw_res);

		assert(res.find("from_extension") != std::string::npos);
	}

	// Test 6: Alias 'files' for 'items'
	{
		std::cout << "6. Alias 'files' for 'items'..." << std::endl;
		nlohmann::json args = {
		    {"files", {{{"path", poem_path}, {"start", 1}, {"end", 1}}, {{"path", poem_path}, {"start", 2}, {"end", 2}}}}};

		std::string raw_res = registry.execute_tool("fs_batch_read", args.dump(), ctx);
		std::string res = fs_utils::unwrap_prompt_untrusted_data_tag(raw_res);

		assert(res.find("### [1/2]") != std::string::npos);
		assert(res.find("### [2/2]") != std::string::npos);
	}

	// Test 7: Error handling for invalid parameters
	{
		std::cout << "7. Error handling for invalid parameters..." << std::endl;
		// Empty items
		{
			nlohmann::json args = {{"items", nlohmann::json::array()}};
			std::string raw_res = registry.execute_tool("fs_batch_read", args.dump(), ctx);
			assert(raw_res.find("Error") != std::string::npos);
		}
		// Invalid type
		{
			nlohmann::json args = {{"items", {{{"path", poem_path}, {"type", "invalid_type"}}}}};
			std::string raw_res = registry.execute_tool("fs_batch_read", args.dump(), ctx);
			assert(raw_res.find("Error:") != std::string::npos);
		}
	}

	std::cout << "All fs_batch_read unit tests passed successfully!" << std::endl;
	return 0;
}
