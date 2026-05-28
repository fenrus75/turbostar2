#include <cassert>
#include <iostream>
#include <memory>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <nlohmann/json.hpp>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/event_queue.h"
#include "../../src/fs_utils.h"

void test_tool_call_recovery()
{
	// 1. Setup temporary home directory
	std::string test_dir = "/tmp/turbostar_test_agent_recovery";
	std::filesystem::create_directories(test_dir);
	setenv("HOME", test_dir.c_str(), 1);

	std::string history_dir = fs_utils::get_project_history_dir("TestAgent");
	std::filesystem::create_directories(history_dir);

	// 2. Write active_state.json with a pending tool call
	std::string filepath = history_dir + "/active_state.json";
	nlohmann::json mock_state = {
		{"conversation", nlohmann::json::array({
			{{"role", "system"}, {"content", "System prompt"}},
			{{"role", "user"}, {"content", "Hello"}},
			{{"role", "assistant"}, {"content", "I will write a file."}, {"tool_calls", nlohmann::json::array({
				{{"id", "call_xyz"}, {"type", "function"}, {"function", {{"name", "fs_write_file"}, {"arguments", "arg_str"}}}}
			})}}
		})}
	};

	std::ofstream out(filepath);
	out << mock_state.dump();
	out.close();

	// 3. Create the agent and load active state
	event_queue q;
	auto model = std::make_shared<agentlib::ai_model>("test-model", "Test Model", "http://localhost", "Test", 0.0, 0.0);
	auto agent = agentlib::ai_agent::create(1, "TestAgent", model, &q, nullptr);

	bool loaded = agent->load_active_state();
	assert(loaded);

	// 4. Verify conversation is appended with the abort response
	auto convo = agent->get_conversation();
	// Convo should now have 4 messages: system, user, assistant (with tool_calls), tool (abort)
	assert(convo.size() == 4);

	const auto& last_msg = convo.back();
	assert(last_msg.role == "tool");
	assert(last_msg.tool_call_id.has_value());
	assert(*last_msg.tool_call_id == "call_xyz");
	assert(last_msg.name.has_value());
	assert(*last_msg.name == "fs_write_file");
	assert(last_msg.content.find("Tool execution aborted") != std::string::npos);

	// Clean up
	std::filesystem::remove_all(test_dir);
	std::cout << "test_tool_call_recovery unit test passed successfully!" << std::endl;
}

int main()
{
	event_queue q;
	auto model = std::make_shared<agentlib::ai_model>("test-model", "Test Model", "http://localhost", "Test", 0.0, 0.0);
	auto agent = agentlib::ai_agent::create(1, "TestAgent", model, &q, nullptr);

	agent->add_todo("Task 1");
	agent->add_todo("Task 2");

	auto todos = agent->get_todos();
	assert(todos.size() == 2);
	assert(todos[0].text == "Task 1");
	assert(todos[1].text == "Task 2");

	auto popped1 = agent->pop_todo();
	assert(popped1.has_value());
	assert(*popped1 == "Task 1");

	todos = agent->get_todos();
	assert(todos.size() == 1);
	assert(todos[0].text == "Task 2");

	auto popped2 = agent->pop_todo();
	assert(popped2.has_value());
	assert(*popped2 == "Task 2");

	auto popped3 = agent->pop_todo();
	assert(!popped3.has_value());

	std::cout << "pop_todo unit test passed successfully!" << std::endl;

	test_tool_call_recovery();

	return 0;
}
