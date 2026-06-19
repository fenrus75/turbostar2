#include "test_watchdog.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/codereview_manager.h"
#include "../../src/event_queue.h"
#include "../../src/fs_utils.h"
#include "../../src/project_manager.h"

using namespace agentlib;

void test_create_tool_execution()
{
	std::cout << "Testing create_code_review_item tool..." << std::endl;

	std::filesystem::path orig_path = std::filesystem::current_path();
	std::filesystem::path temp_proj = orig_path / "test_temp_tool_proj";
	std::filesystem::remove_all(temp_proj);
	std::filesystem::create_directories(temp_proj);

	// Create a mock source file
	std::filesystem::path mock_file = temp_proj / "src_test.cpp";
	std::ofstream out(mock_file);
	assert(out.is_open());
	out << "line 1\nline 2\nline 3\nline 4\n";
	out.close();

	fs_utils::set_override_project_dir(temp_proj.string());
	codereview_manager::get_instance().load_project(temp_proj.string());
	codereview_manager::get_instance().clear_all();

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

	// Test 1: Happy path with auto line lookup
	std::string args_json = "{"
				"\"summary\": \"Lookout issue\","
				"\"filename\": \"src_test.cpp\","
				"\"line_number\": 3,"
				"\"severity\": \"medium\","
				"\"description\": \"Test description\""
				"}";

	std::string res_str = registry.execute_tool("create_code_review_item", args_json, ctx);
	std::cout << "Result: " << res_str << std::endl;

	nlohmann::json res_j = nlohmann::json::parse(res_str);
	assert(res_j["id"].get<int>() == 1);
	assert(res_j["status"].get<std::string>() == "created");
	assert(res_j["line_content"].get<std::string>() == "line 3");

	// Verify manager stored it
	auto item_opt = codereview_manager::get_instance().get_code_review_item(1);
	assert(item_opt.has_value());
	assert(item_opt->summary == "Lookout issue");
	assert(item_opt->line_content == "line 3");

	// Verify parent agent received context injection
	auto parent_history = agent->get_conversation();
	assert(!parent_history.empty());
	assert(parent_history.back().role == "user");
	assert(parent_history.back().content.find("Subagent created a code review item (ID: 1)") != std::string::npos);

	// Verify global event queue received codereview_updated event
	auto ev_opt = q.pop();
	assert(ev_opt.has_value());
	assert(ev_opt->type == event_type::codereview_updated);
	assert(ev_opt->key_code == 1);

	// Test 2: Validation errors
	// Invalid severity
	std::string bad_severity_args = "{"
					"\"summary\": \"Lookout issue\","
					"\"filename\": \"src_test.cpp\","
					"\"line_number\": 3,"
					"\"severity\": \"ultra-critical\","
					"\"description\": \"Test description\""
					"}";
	auto prep = registry.prepare_tool("create_code_review_item", bad_severity_args, ctx);
	assert(prep.tool == nullptr);
	assert(!prep.error_message.empty());

	// Unallowed filename (outside temp_proj)
	std::string bad_file_args = "{"
				    "\"summary\": \"Lookout issue\","
				    "\"filename\": \"/etc/passwd\","
				    "\"line_number\": 1,"
				    "\"severity\": \"high\","
				    "\"description\": \"Test description\""
				    "}";
	prep = registry.prepare_tool("create_code_review_item", bad_file_args, ctx);
	assert(prep.tool == nullptr);
	assert(!prep.error_message.empty());

	// Invalid negative line number
	std::string bad_line_args = "{"
				    "\"summary\": \"Lookout issue\","
				    "\"filename\": \"src_test.cpp\","
				    "\"line_number\": -1,"
				    "\"severity\": \"high\","
				    "\"description\": \"Test description\""
				    "}";
	prep = registry.prepare_tool("create_code_review_item", bad_line_args, ctx);
	assert(prep.tool == nullptr);
	assert(!prep.error_message.empty());

	// Cleanup
	fs_utils::set_override_project_dir("");
	std::filesystem::remove_all(temp_proj);
	std::cout << "create_code_review_item tool verified successfully!" << std::endl;
}

int main()
{
	test_watchdog::setup_watchdog(30);
	project_manager::get_instance().initialize();
	test_create_tool_execution();
	return 0;
}
