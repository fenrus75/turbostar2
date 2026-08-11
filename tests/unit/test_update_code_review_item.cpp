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

void test_update_tool_execution()
{
	std::cout << "Testing update_code_review_item tool..." << std::endl;

	std::filesystem::path orig_path = std::filesystem::current_path();
	std::filesystem::path temp_proj = orig_path / "test_temp_update_proj";
	std::filesystem::remove_all(temp_proj);
	std::filesystem::create_directories(temp_proj);

	fs_utils::set_override_project_dir(temp_proj.string());
	codereview_manager::get_instance().load_project(temp_proj.string());
	codereview_manager::get_instance().clear_all();

	// Pre-create an item so we can update it
	int item_id = codereview_manager::get_instance().create_code_review_item(
	    "Initial issue",
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

	// Test 1: Happy path update
	std::string args_json = "{"
	                        "\"id\": 1,"
	                        "\"state\": \"confirmed\","
	                        "\"severity\": \"high\","
	                        "\"description\": \"Updated description\","
	                        "\"proposed_fix\": \"Updated proposed fix\""
	                        "}";

	std::string res_str = registry.execute_tool("update_code_review_item", args_json, ctx);
	std::cout << "Result: " << res_str << std::endl;

	nlohmann::json res_j = nlohmann::json::parse(res_str);
	assert(res_j["id"].get<int>() == 1);
	assert(res_j["status"].get<std::string>() == "updated");

	// Verify manager stored the updates
	auto item_opt = codereview_manager::get_instance().get_code_review_item(1);
	assert(item_opt.has_value());
	assert(item_opt->state == "confirmed");
	assert(item_opt->severity == "high");
	assert(item_opt->description == "Updated description");
	assert(item_opt->proposed_fix == "Updated proposed fix");

	// Verify parent agent received NO context injection (update is silent bookkeeping by design)
	auto parent_history = agent->get_conversation();
	for (const auto &msg : parent_history) {
		assert(msg.content.find("Subagent updated code review item") == std::string::npos);
	}

	// Verify global event queue received codereview_updated event
	auto ev_opt = q.pop();
	assert(ev_opt.has_value());
	assert(ev_opt->type == event_type::codereview_updated);
	assert(ev_opt->key_code == 1);

	// Test 2: Validation errors
	// Non-existent ID
	std::string bad_id_args = "{"
	                          "\"id\": 999,"
	                          "\"state\": \"confirmed\""
	                          "}";
	res_str = registry.execute_tool("update_code_review_item", bad_id_args, ctx);
	// Failure returns are prefixed with "Error: " so the run-loop classifies them as failed
	assert(res_str.starts_with("Error: "));
	nlohmann::json bad_id_res = nlohmann::json::parse(res_str.substr(7));
	assert(bad_id_res["status"].get<std::string>() == "error");

	// Invalid state value
	std::string bad_state_args = "{"
	                             "\"id\": 1,"
	                             "\"state\": \"completed\""
	                             "}";
	auto prep = registry.prepare_tool("update_code_review_item", bad_state_args, ctx);
	assert(prep.tool == nullptr);
	assert(!prep.error_message.empty());

	// Invalid severity value
	std::string bad_severity_args = "{"
	                                "\"id\": 1,"
	                                "\"severity\": \"ultra-critical\""
	                                "}";
	prep = registry.prepare_tool("update_code_review_item", bad_severity_args, ctx);
	assert(prep.tool == nullptr);
	assert(!prep.error_message.empty());

	// Cleanup
	fs_utils::set_override_project_dir("");
	std::filesystem::remove_all(temp_proj);
	std::cout << "update_code_review_item tool verified successfully!" << std::endl;
}

int main()
{
	test_watchdog::setup_watchdog(30);
	project_manager::get_instance().initialize();
	test_update_tool_execution();
	return 0;
}
