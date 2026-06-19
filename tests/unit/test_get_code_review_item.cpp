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

void test_get_tool_execution()
{
	std::cout << "Testing get_code_review_item tool..." << std::endl;

	std::filesystem::path orig_path = std::filesystem::current_path();
	std::filesystem::path temp_proj = orig_path / "test_temp_get_proj";
	std::filesystem::remove_all(temp_proj);
	std::filesystem::create_directories(temp_proj);

	fs_utils::set_override_project_dir(temp_proj.string());
	codereview_manager::get_instance().load_project(temp_proj.string());
	codereview_manager::get_instance().clear_all();

	// Create a new item (unresolved, state "new")
	int id1 = codereview_manager::get_instance().create_code_review_item(
	    "Null Deref", "src/main.cpp", 10, "int* p = nullptr;", "high", "Detailed desc", "Add check"
	);
	assert(id1 == 1);

	// Create another item and resolve it
	int id2 = codereview_manager::get_instance().create_code_review_item(
	    "Leaked Memory", "src/heap.cpp", 20, "void* q = malloc(10);", "medium", "Leak desc", "free(q)"
	);
	assert(id2 == 2);
	codereview_manager::get_instance().resolve_code_review_item(id2, "commit123");

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;
	event_queue q;

	ctx.fs_security.set_working_directory(temp_proj.string());
	ctx.fs_security.add_allowed_root(temp_proj.string(), access_type::read);
	ctx.fs_security.add_allowed_root(temp_proj.string(), access_type::write);
	ctx.queue = &q;

	auto model = std::make_shared<ai_model>("test-model", "Test Model", "http://localhost", "Test", 0.0, 0.0);
	auto agent = ai_agent::create(101, "GetAgent", model, &q, nullptr);
	ctx.active_agent = agent.get();

	// Test 1: Developer role gets unresolved item (allowed)
	agent->set_role(agent_role::developer);
	std::string args_json = "{\"id\": 1}";
	std::string res_str = registry.execute_tool("get_code_review_item", args_json, ctx);
	std::cout << "Result Developer id1:\n" << res_str << std::endl;

	nlohmann::json res_j = nlohmann::json::parse(res_str);
	assert(res_j["id"].get<int>() == 1);
	assert(res_j["state"].get<std::string>() == "new");
	assert(res_j["summary"].get<std::string>() == "Null Deref");

	// Test 2: Developer role gets resolved item (denied)
	args_json = "{\"id\": 2}";
	res_str = registry.execute_tool("get_code_review_item", args_json, ctx);
	std::cout << "Result Developer id2 (denied):\n" << res_str << std::endl;
	res_j = nlohmann::json::parse(res_str);
	assert(res_j.contains("status"));
	assert(res_j["status"].get<std::string>() == "error");
	assert(res_j["message"].get<std::string>().find("restricted") != std::string::npos);

	// Test 3: Verifier role gets resolved item (allowed)
	agent->set_role(agent_role::verifier);
	res_str = registry.execute_tool("get_code_review_item", args_json, ctx);
	std::cout << "Result Verifier id2 (allowed):\n" << res_str << std::endl;
	res_j = nlohmann::json::parse(res_str);
	assert(res_j["id"].get<int>() == 2);
	assert(res_j["state"].get<std::string>() == "resolved");
	assert(res_j["resolved_in_commit"].get<std::string>() == "commit123");

	// Cleanup
	fs_utils::set_override_project_dir("");
	std::filesystem::remove_all(temp_proj);
	std::cout << "get_code_review_item tool verified successfully!" << std::endl;
}

int main()
{
	test_watchdog::setup_watchdog(30);
	project_manager::get_instance().initialize();
	test_get_tool_execution();
	return 0;
}
