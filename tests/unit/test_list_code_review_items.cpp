#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include <filesystem>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/project_manager.h"
#include "../../src/event_queue.h"
#include "../../src/codereview_manager.h"
#include "../../src/fs_utils.h"

using namespace agentlib;

void test_list_tool_execution()
{
	std::cout << "Testing list_code_review_items tool..." << std::endl;

	std::filesystem::path orig_path = std::filesystem::current_path();
	std::filesystem::path temp_proj = orig_path / "test_temp_list_proj";
	std::filesystem::remove_all(temp_proj);
	std::filesystem::create_directories(temp_proj);

	fs_utils::set_override_project_dir(temp_proj.string());
	codereview_manager::get_instance().load_project(temp_proj.string());
	codereview_manager::get_instance().clear_all();

	// 1. Create items in different directories and with different states
	int id1 = codereview_manager::get_instance().create_code_review_item(
	    "Null Deref", "src/ui/window.cpp", 10, "int* p = nullptr;", "high", "Desc1", "Fix1"
	);
	int id2 = codereview_manager::get_instance().create_code_review_item(
	    "Leaked Memory", "src/utils/heap.cpp", 20, "void* q = malloc(10);", "medium", "Desc2", "Fix2"
	);
	int id3 = codereview_manager::get_instance().create_code_review_item(
	    "Buffer Overflow", "tests/test.cpp", 5, "char buf[2]; strcpy(buf, s);", "critical", "Desc3", "Fix3"
	);

	assert(id1 == 1);
	assert(id2 == 2);
	assert(id3 == 3);

	// Resolve id2 so we can test include_resolved filtering
	codereview_manager::get_instance().resolve_code_review_item(id2, "commit456");

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;
	event_queue q;

	ctx.fs_security.set_working_directory(temp_proj.string());
	ctx.fs_security.add_allowed_root(temp_proj.string(), access_type::read);
	ctx.fs_security.add_allowed_root(temp_proj.string(), access_type::write);
	ctx.queue = &q;

	auto model = std::make_shared<ai_model>("test-model", "Test Model", "http://localhost", "Test", 0.0, 0.0);
	auto agent = ai_agent::create(101, "ListAgent", model, &q, nullptr);
	ctx.active_agent = agent.get();

	// Test 1: List without filter (developer role, should not see resolved id2)
	agent->set_role(agent_role::developer);
	std::string args_json = "{}";
	std::string res_str = registry.execute_tool("list_code_review_items", args_json, ctx);
	std::cout << "Result Developer all:\n" << res_str << std::endl;

	assert(res_str.find("Null Deref") != std::string::npos);
	assert(res_str.find("Buffer Overflow") != std::string::npos);
	assert(res_str.find("Leaked Memory") == std::string::npos); // Should be filtered out

	// Test 2: Try to force include_resolved as developer role (should still be overridden/excluded)
	args_json = "{\"include_resolved\": true}";
	res_str = registry.execute_tool("list_code_review_items", args_json, ctx);
	assert(res_str.find("Leaked Memory") == std::string::npos);

	// Test 3: List as verifier role with include_resolved (should see all items)
	agent->set_role(agent_role::verifier);
	res_str = registry.execute_tool("list_code_review_items", args_json, ctx);
	std::cout << "Result Verifier include_resolved:\n" << res_str << std::endl;
	assert(res_str.find("Leaked Memory") != std::string::npos);

	// Test 4: Prefix match on filename (e.g. "src/")
	args_json = "{\"filename\": \"src/\", \"include_resolved\": true}";
	res_str = registry.execute_tool("list_code_review_items", args_json, ctx);
	std::cout << "Result prefix src/:\n" << res_str << std::endl;
	assert(res_str.find("src/ui/window.cpp") != std::string::npos);
	assert(res_str.find("src/utils/heap.cpp") != std::string::npos);
	assert(res_str.find("tests/test.cpp") == std::string::npos);

	// Test 5: Exact filename match (e.g. "tests/test.cpp")
	args_json = "{\"filename\": \"tests/test.cpp\"}";
	res_str = registry.execute_tool("list_code_review_items", args_json, ctx);
	std::cout << "Result exact tests/test.cpp:\n" << res_str << std::endl;
	assert(res_str.find("tests/test.cpp") != std::string::npos);
	assert(res_str.find("src/") == std::string::npos);

	// Cleanup
	fs_utils::set_override_project_dir("");
	std::filesystem::remove_all(temp_proj);
	std::cout << "list_code_review_items tool verified successfully!" << std::endl;
}

int main()
{
	test_watchdog::setup_watchdog(30);
	project_manager::get_instance().initialize();
	test_list_tool_execution();
	return 0;
}
