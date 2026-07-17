#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include <nlohmann/json.hpp>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/project_manager.h"

using namespace agentlib;

int main()
{
	test_watchdog::setup_watchdog(30);
	project_manager::get_instance().initialize();

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;

	// 1. Success case: execute list_tool_calls
	{
		std::string execute_result = registry.execute_tool("list_tool_calls", "{}", ctx);
		std::cout << "Execution Result:\n" << execute_result << "\n";
		assert(!execute_result.empty());
		assert(execute_result.find("list_tool_calls") != std::string::npos);
	}

	// 2. Test unexpected parameter rejection (should fail validation)
	{
		auto prep = registry.prepare_tool("list_tool_calls", "{\"unexpected_arg\": 123}", ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	// 3. Test filtering with search parameter
	{
		std::string execute_result = registry.execute_tool("list_tool_calls", "{\"search\": \"list_tool_calls\"}", ctx);
		std::cout << "Search Result:\n" << execute_result << "\n";
		assert(!execute_result.empty());
		assert(execute_result.find("list_tool_calls") != std::string::npos);
		// Check that other unrelated tools are not present
		assert(execute_result.find("git_blame") == std::string::npos);
	}

	// 4. Test detailed formatting with show_details parameter
	{
		std::string execute_result = registry.execute_tool("list_tool_calls", "{\"show_details\": true}", ctx);
		std::cout << "Show Details Result:\n" << execute_result << "\n";
		assert(!execute_result.empty());
		assert(execute_result.find("### `list_tool_calls`") != std::string::npos);
		assert(execute_result.find("* **Description:**") != std::string::npos);
		assert(execute_result.find("* **Arguments:**") != std::string::npos);
	}

	std::cout << "list_tool_calls tests passed successfully.\n";
	return 0;
}
