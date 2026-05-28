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

	// 5. Test mismatched tool response ordering reordering/normalization
	nlohmann::json mock_state_mismatched = {
		{"conversation", nlohmann::json::array({
			{{"role", "system"}, {"content", "System prompt"}},
			{{"role", "user"}, {"content", "Hello"}},
			{{"role", "assistant"}, {"content", "I will write a file."}, {"tool_calls", nlohmann::json::array({
				{{"id", "call_mismatch"}, {"type", "function"}, {"function", {{"name", "fs_write_file"}, {"arguments", "arg_str"}}}}
			})}},
			{{"role", "user"}, {"content", "Follow-up question"}},
			{{"role", "assistant"}, {"content", "Follow-up answer"}},
			{{"role", "tool"}, {"content", "Mock tool success response"}, {"tool_call_id", "call_mismatch"}, {"name", "fs_write_file"}}
		})}
	};

	std::ofstream out2(filepath);
	out2 << mock_state_mismatched.dump();
	out2.close();

	auto agent2 = agentlib::ai_agent::create(1, "TestAgent", model, &q, nullptr);
	bool loaded2 = agent2->load_active_state();
	assert(loaded2);

	auto convo2 = agent2->get_conversation();
	// Convo2 should have 6 messages: system, user, assistant (with tool_calls), tool (success, reordered here), user, assistant
	assert(convo2.size() == 6);

	// The 4th message (index 3) should now be the tool response!
	assert(convo2[3].role == "tool");
	assert(convo2[3].tool_call_id.has_value());
	assert(*convo2[3].tool_call_id == "call_mismatch");
	assert(convo2[3].content == "Mock tool success response");

	// The 5th and 6th messages should be the follow-up user and assistant messages
	assert(convo2[4].role == "user");
	assert(convo2[4].content == "Follow-up question");
	assert(convo2[5].role == "assistant");
	assert(convo2[5].content == "Follow-up answer");

	// 6. Test orphaned tool response discarding
	nlohmann::json mock_state_orphaned = {
		{"conversation", nlohmann::json::array({
			{{"role", "system"}, {"content", "System prompt"}},
			{{"role", "user"}, {"content", "Hello"}},
			{{"role", "tool"}, {"content", "Orphaned tool response"}, {"tool_call_id", "call_orphan"}, {"name", "fs_read_lines"}}
		})}
	};

	std::ofstream out3(filepath);
	out3 << mock_state_orphaned.dump();
	out3.close();

	auto agent3 = agentlib::ai_agent::create(1, "TestAgent", model, &q, nullptr);
	bool loaded3 = agent3->load_active_state();
	assert(loaded3);

	auto convo3 = agent3->get_conversation();
	// Convo3 should have only 2 messages: system, user. The orphaned tool response must be discarded!
	assert(convo3.size() == 2);
	assert(convo3[0].role == "system");
	assert(convo3[1].role == "user");

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
