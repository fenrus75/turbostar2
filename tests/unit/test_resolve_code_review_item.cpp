// Tested source file: src/tools/resolve_code_review_item/resolve_code_review_item_entry.cpp
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

void test_resolve_tool_execution()
{
	std::cout << "Testing resolve_code_review_item tool..." << std::endl;

	std::filesystem::path orig_path = std::filesystem::current_path();
	std::filesystem::path temp_proj = orig_path / "test_temp_resolve_proj";
	std::filesystem::remove_all(temp_proj);
	std::filesystem::create_directories(temp_proj);

	fs_utils::set_override_project_dir(temp_proj.string());
	codereview_manager::get_instance().load_project(temp_proj.string());
	codereview_manager::get_instance().clear_all();

	// Pre-create an item so we can resolve it
	int item_id = codereview_manager::get_instance().create_code_review_item(
	    "Memory Leak",
	    "src/leak.cpp",
	    42,
	    "char* p = new char[10];",
	    "high",
	    "Leak description",
	    "delete[] p;"
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
	auto subagent = ai_agent::create(101, "DeveloperAgent", model, &q, nullptr);
	subagent->set_parent(agent);
	ctx.active_agent = subagent.get();
	ctx.properties = subagent->get_properties();

	// Test 1: Role rejection (reviewer role should not be allowed)
	subagent->set_role(agent_role::reviewer);
	ctx.properties = subagent->get_properties();
	std::string args_json = "{\"id\": 1, \"commit_hash\": \"abcdef123456\"}";
	auto prep = registry.prepare_tool("resolve_code_review_item", args_json, ctx);
	assert(prep.tool == nullptr);
	assert(!prep.error_message.empty());

	// Test 2: Role allowed (developer role)
	subagent->set_role(agent_role::developer);
	ctx.properties = subagent->get_properties();
	std::string res_str = registry.execute_tool("resolve_code_review_item", args_json, ctx);
	std::cout << "Result: " << res_str << std::endl;

	nlohmann::json res_j = nlohmann::json::parse(fs_utils::unwrap_prompt_untrusted_data_tag(res_str));
	assert(res_j["id"].get<int>() == 1);
	assert(res_j["status"].get<std::string>() == "resolved");

	// Verify manager stored the resolved state and commit hash
	auto item_opt = codereview_manager::get_instance().get_code_review_item(1);
	assert(item_opt.has_value());
	assert(item_opt->state == "resolved");
	assert(item_opt->resolved_in_commit == "abcdef123456");

	// Verify parent agent received NO context injection (resolve is silent bookkeeping by design)
	auto parent_history = agent->get_conversation();
	for (const auto &msg : parent_history) {
		assert(msg.content.find("Subagent resolved code review item") == std::string::npos);
	}

	// Verify global event queue received codereview_updated event
	auto ev_opt = q.pop();
	assert(ev_opt.has_value());
	assert(ev_opt->type == event_type::codereview_updated);
	assert(ev_opt->payload == "1");

	// Test 3: item_id parameter (canonical name)
	int id2 = codereview_manager::get_instance().create_code_review_item(
	    "Resource Leak", "src/file.cpp", 12, "FILE* f = fopen(...);", "medium", "Desc", "fclose(f);"
	);
	assert(id2 == 2);
	std::string args_item_id = "{\"item_id\": 2, \"commit_hash\": \"112233445566\"}";
	std::string res_item_id = registry.execute_tool("resolve_code_review_item", args_item_id, ctx);
	nlohmann::json j_item_id = nlohmann::json::parse(fs_utils::unwrap_prompt_untrusted_data_tag(res_item_id));
	assert(j_item_id["status"].get<std::string>() == "resolved");
	assert(j_item_id.contains("item_id") && j_item_id["item_id"].get<int>() == 2);

	// Test 4: Batch resolution with item_ids array
	int id3 = codereview_manager::get_instance().create_code_review_item(
	    "Issue 3", "src/a.cpp", 1, "code", "low", "Desc3", "Fix3"
	);
	int id4 = codereview_manager::get_instance().create_code_review_item(
	    "Issue 4", "src/b.cpp", 2, "code", "low", "Desc4", "Fix4"
	);
	assert(id3 == 3);
	assert(id4 == 4);
	std::string args_batch = "{\"item_ids\": [3, 4], \"commit_hash\": \"aabbccddeeff\"}";
	std::string res_batch = registry.execute_tool("resolve_code_review_item", args_batch, ctx);
	nlohmann::json j_batch = nlohmann::json::parse(fs_utils::unwrap_prompt_untrusted_data_tag(res_batch));
	assert(j_batch["status"].get<std::string>() == "resolved");
	assert(j_batch.contains("resolved_items") || j_batch.contains("item_ids"));
	auto item3_opt = codereview_manager::get_instance().get_code_review_item(3);
	auto item4_opt = codereview_manager::get_instance().get_code_review_item(4);
	assert(item3_opt.has_value() && item3_opt->state == "resolved");
	assert(item4_opt.has_value() && item4_opt->state == "resolved");

	// Cleanup
	fs_utils::set_override_project_dir("");
	std::filesystem::remove_all(temp_proj);
	std::cout << "resolve_code_review_item tool verified successfully!" << std::endl;
}

int main()
{
	test_watchdog::setup_watchdog(30);
	project_manager::get_instance().initialize();
	test_resolve_tool_execution();
	return 0;
}
