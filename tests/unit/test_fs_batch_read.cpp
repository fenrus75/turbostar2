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
		assert(res.find("<fs_read_symbol_result>") == std::string::npos);
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

	// Test 7: Tolerant single item at top level or single object in items
	{
		std::cout << "7. Tolerant single item at top level and items object..." << std::endl;

		// 7a: Top level path, start, end without 'items' wrapper
		{
			nlohmann::json args = {{"path", poem_path}, {"start", 1}, {"end", 2}};
			std::string raw_res = registry.execute_tool("fs_batch_read", args.dump(), ctx);
			std::string res = fs_utils::unwrap_prompt_untrusted_data_tag(raw_res);

			assert(res.find("### [1/1]") == std::string::npos);
			assert(res.find("Code for lines 1 - ") != std::string::npos);
			assert(res.find("1:") != std::string::npos);
		}

		// 7b: Top level symbol without 'items' wrapper
		{
			nlohmann::json args = {{"path", "src/mime.cpp"}, {"type", "symbol"}, {"name", "from_extension"}};
			std::string raw_res = registry.execute_tool("fs_batch_read", args.dump(), ctx);
			std::string res = fs_utils::unwrap_prompt_untrusted_data_tag(raw_res);

			assert(res.find("from_extension") != std::string::npos);
			assert(res.find("<fs_read_symbol_result>") == std::string::npos);
		}

		// 7c: Top level aliases: file, start_line, end_line
		{
			nlohmann::json args = {{"file", poem_path}, {"start_line", 2}, {"end_line", 3}};
			std::string raw_res = registry.execute_tool("fs_batch_read", args.dump(), ctx);
			std::string res = fs_utils::unwrap_prompt_untrusted_data_tag(raw_res);

			assert(res.find("2:") != std::string::npos);
			assert(res.find("3:") != std::string::npos);
		}

		// 7d: Single object passed directly to 'items' instead of an array
		{
			nlohmann::json args = {{"items", {{"path", poem_path}, {"start", 1}, {"end", 2}}}};
			std::string raw_res = registry.execute_tool("fs_batch_read", args.dump(), ctx);
			std::string res = fs_utils::unwrap_prompt_untrusted_data_tag(raw_res);

			assert(res.find("1:") != std::string::npos);
		}
	}

	// Test 8: Error handling for invalid parameters
	{
		std::cout << "8. Error handling for invalid parameters..." << std::endl;
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

	// Test 9: Rate-limited fs_batch_read recommendation tip in fs_read_lines
	{
		std::cout << "9. Rate-limited fs_batch_read tip in fs_read_lines..." << std::endl;
		tool_context tip_ctx;
		tip_ctx.fs_security.set_working_directory(project_root);
		tip_ctx.fs_security.add_allowed_root(project_root, access_type::read);
		tip_ctx.active_agent = agent.get();

		// Reading a partial range of a file with >= 2 symbols (e.g. src/mime.h)
		nlohmann::json args = {{"path", "src/mime.h"}, {"start_line", 1}, {"end_line", 20}};

		// Call 1: First time should show tip
		std::string res1 = registry.execute_tool("fs_read_lines", args.dump(), tip_ctx);
		assert(
		    res1.find("*Tip: Fetch multiple symbols, line ranges, or class contexts from above in one turn using fs_batch_read") !=
		    std::string::npos);

		// Call 2: Immediate second call within 5 minutes should NOT show tip
		std::string res2 = registry.execute_tool("fs_read_lines", args.dump(), tip_ctx);
		assert(
		    res2.find("*Tip: Fetch multiple symbols, line ranges, or class contexts from above in one turn using fs_batch_read") ==
		    std::string::npos);

		// Call 3: Setting last_batch_read_tip_time 6 minutes in the past should show tip again
		tip_ctx.last_batch_read_tip_time = std::chrono::steady_clock::now() - std::chrono::minutes(6);
		std::string res3 = registry.execute_tool("fs_read_lines", args.dump(), tip_ctx);
		assert(
		    res3.find("*Tip: Fetch multiple symbols, line ranges, or class contexts from above in one turn using fs_batch_read") !=
		    std::string::npos);
	}

	std::cout << "All fs_batch_read unit tests passed successfully!" << std::endl;
	return 0;
}
