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

void test_confirm_tool_execution()
{
	std::cout << "Testing confirm_code_review_item tool..." << std::endl;

	std::filesystem::path orig_path = std::filesystem::current_path();
	std::filesystem::path temp_proj = orig_path / "test_temp_confirm_proj";
	std::filesystem::remove_all(temp_proj);
	std::filesystem::create_directories(temp_proj);

	fs_utils::set_override_project_dir(temp_proj.string());
	codereview_manager::get_instance().load_project(temp_proj.string());
	codereview_manager::get_instance().clear_all();

	// Pre-create an item so we can confirm it
	int item_id = codereview_manager::get_instance().create_code_review_item(
	    "Vulnerability",
	    "src/test.cpp",
	    10,
	    "int x = 0;",
	    "low",
	    "Initial description",
	    "Change x"
	);
	assert(item_id == 1);

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;
	event_queue q;

	ctx.fs_security.set_working_directory(temp_proj.string());
	ctx.fs_security.add_allowed_root(temp_proj.string(), access_type::read);
	ctx.fs_security.add_allowed_root(temp_proj.string(), access_type::write);
	ctx.queue = &q;

	auto model = std::make_shared<ai_model>("test-model", "Test Model", "http://localhost", "Test", 0.0, 0.0);
	auto agent = ai_agent::create(1, "ParentAgent", model, &q, nullptr);
	auto subagent = ai_agent::create(101, "ReviewAgent", model, &q, nullptr);
	subagent->set_parent(agent);
	ctx.active_agent = subagent.get();

	// Test 1: Role rejection (default role is developer, confirm tool is verifier-only)
	subagent->set_role(agent_role::developer);
	std::string args_json = "{\"id\": 1}";
	auto prep = registry.prepare_tool("confirm_code_review_item", args_json, ctx);
	assert(prep.tool == nullptr);
	assert(!prep.error_message.empty());

	// Test 2: Role allowed (verifier role)
	subagent->set_role(agent_role::verifier);
	std::string res_str = registry.execute_tool("confirm_code_review_item", args_json, ctx);
	std::cout << "Result: " << res_str << std::endl;

	nlohmann::json res_j = nlohmann::json::parse(res_str);
	assert(res_j["id"].get<int>() == 1);
	assert(res_j["status"].get<std::string>() == "confirmed");

	// Verify manager stored the confirmed state
	auto item_opt = codereview_manager::get_instance().get_code_review_item(1);
	assert(item_opt.has_value());
	assert(item_opt->state == "confirmed");

	// Verify parent agent received context injection
	auto parent_history = agent->get_conversation();
	assert(!parent_history.empty());
	assert(parent_history.back().role == "user");
	assert(parent_history.back().content.find("Subagent confirmed/verified code review item (ID: 1)") != std::string::npos);

	// Verify global event queue received codereview_updated event
	auto ev_opt = q.pop();
	assert(ev_opt.has_value());
	assert(ev_opt->type == event_type::codereview_updated);
	assert(ev_opt->key_code == 1);

	// Test 3: Transitions from resolved -> verified-fixed
	// Mark it resolved first via the manager
	codereview_manager::get_instance().resolve_code_review_item(1, "commit123");
	res_str = registry.execute_tool("confirm_code_review_item", args_json, ctx);
	res_j = nlohmann::json::parse(res_str);
	assert(res_j["id"].get<int>() == 1);
	assert(res_j["status"].get<std::string>() == "confirmed"); // Status field in tool returns "confirmed" on success

	item_opt = codereview_manager::get_instance().get_code_review_item(1);
	assert(item_opt->state == "verified-fixed");

	// Cleanup
	fs_utils::set_override_project_dir("");
	std::filesystem::remove_all(temp_proj);
	std::cout << "confirm_code_review_item tool verified successfully!" << std::endl;
}

int main()
{
	project_manager::get_instance().initialize();
	test_confirm_tool_execution();
	return 0;
}
