#include <cassert>
#include <iostream>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/compaction_engine.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/config_manager.h"
#include "../../src/event_queue.h"
#include "../../src/git_manager.h"
#include "../../src/project_manager.h"
#include "tools/agent_add_todo/agent_add_todo.h"

using namespace agentlib;

int main()
{
	// Initialize managers
	project_manager::get_instance().initialize();

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;
	event_queue q;

	ctx.fs_security.set_working_directory(project_manager::get_instance().get_project_root());
	ctx.fs_security.add_allowed_root(project_manager::get_instance().get_project_root(), access_type::read);
	ctx.fs_security.add_allowed_root(project_manager::get_instance().get_project_root(), access_type::write);

	std::cout << "Testing fs_list_tests..." << std::endl;
	std::string list_result = registry.execute_tool("fs_list_tests", "{}", ctx);
	std::cout << "Result:\n" << list_result << std::endl;

	assert(list_result.find("Available Tests") != std::string::npos);
	assert(list_result.find("unit_event_logger") != std::string::npos);

	// Test 1: Pattern filtering success
	std::string list_filtered = registry.execute_tool("fs_list_tests", "{\"pattern\": \"logger\"}", ctx);
	assert(list_filtered.find("unit_event_logger") != std::string::npos);

	// Test 2: Pattern filtering no matches
	std::string list_none = registry.execute_tool("fs_list_tests", "{\"pattern\": \"nonexistent_pattern_xyz\"}", ctx);
	assert(list_none.find("No tests matching the pattern") != std::string::npos);

	// Test 3: Invalid regex pattern
	{
		auto prep = registry.prepare_tool("fs_list_tests", "{\"pattern\": \"[\"}", ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	// Test 4: Unexpected arguments
	{
		auto prep = registry.prepare_tool("fs_list_tests", "{\"pattern\": \"logger\", \"unexpected\": 123}", ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	std::cout << "\nTesting fs_run_tests with specific tests..." << std::endl;
	// We'll run a fast test like unit_event_logger
	std::string run_result = registry.execute_tool("fs_run_tests", "{\"test_names\": [\"unit_event_logger\"]}", ctx);
	std::cout << "Result:\n" << run_result << std::endl;

	assert(run_result.find("meson test") != std::string::npos);
	assert(run_result.find("unit_event_logger") != std::string::npos);
	assert(run_result.find("OK") != std::string::npos || run_result.find("PASS") != std::string::npos ||
	       run_result.find("exit status 0") != std::string::npos);

	std::cout << "\nTesting fs_run_tests with space-containing test name..." << std::endl;
	std::string space_run_result = registry.execute_tool("fs_run_tests", "{\"test_names\": [\"test with space\"]}", ctx);
	std::cout << "Space run result:\n" << space_run_result << std::endl;
	assert(space_run_result.find("'test with space'") != std::string::npos || space_run_result.find("\"test with space\"") != std::string::npos);

	std::cout << "\nTesting agent_set_timer..." << std::endl;
	auto model = std::make_shared<ai_model>("test-model", "Test Model", "http://localhost", "Test", 0.0, 0.0);
	auto agent = ai_agent::create(1, "TestAgent", model, &q, nullptr);
	ctx.active_agent = agent.get();
	std::string timer_result = registry.execute_tool("agent_set_timer", "{\"seconds\": 1}", ctx);
	std::cout << "Result:\n" << timer_result << std::endl;
	assert(timer_result.find("Timer set for 1 seconds.") != std::string::npos);

	std::cout << "\nTesting get_memory_index..." << std::endl;
	std::string empty_mem = agent->get_memory_index();
	std::cout << "Empty memory index:\n" << empty_mem << std::endl;
	assert(empty_mem.find("Memory index is empty") != std::string::npos);

	agent->inject_context("user", "Hello world");
	agent->inject_context("assistant", "Hi there!");
	agent->snapshot_episode("Test Episode", "Test Summary", {"test-tag"});

	std::string populated_mem = agent->get_memory_index();
	std::cout << "Populated memory index:\n" << populated_mem << std::endl;
	assert(populated_mem.find("Agent Memory Index") != std::string::npos);
	assert(populated_mem.find("Test Episode") != std::string::npos);
	assert(populated_mem.find("raw") != std::string::npos);
	assert(populated_mem.find("think-free") != std::string::npos);
	assert(populated_mem.find("think-free+pseudo") != std::string::npos);

	std::cout << "\nTesting agent_list_episodes..." << std::endl;
	// 1. Set to trivial to verify filtering
	agent->update_episode_hint("episode_1", "Trivial or extremely brief episode.");
	std::string list_episodes_trivial = registry.execute_tool("list_episodes", "{}", ctx);
	std::cout << "Trivial Result:\n" << list_episodes_trivial << std::endl;
	assert(list_episodes_trivial.find("episode_1") == std::string::npos);
	assert(list_episodes_trivial.find("Trivial or extremely brief") == std::string::npos);

	// 2. Set to non-trivial and verify listing works
	agent->update_episode_hint("episode_1", "Resume when user asks about testing");
	std::string list_episodes_result = registry.execute_tool("agent_list_episodes", "{}", ctx);
	std::cout << "Result:\n" << list_episodes_result << std::endl;
	assert(list_episodes_result.find("| Episode | When to Resume |") != std::string::npos);
	assert(list_episodes_result.find("episode_1") != std::string::npos);
	assert(list_episodes_result.find("Resume when user asks about testing") != std::string::npos);

	std::string list_episodes_alias_result = registry.execute_tool("list_episodes", "{}", ctx);
	std::cout << "Alias Result:\n" << list_episodes_alias_result << std::endl;
	assert(list_episodes_alias_result.find("| Episode | When to Resume |") != std::string::npos);
	assert(list_episodes_alias_result.find("episode_1") != std::string::npos);
	assert(list_episodes_alias_result.find("Resume when user asks about testing") != std::string::npos);

	std::cout << "\nTesting inject_archived_episodes_summary..." << std::endl;
	// With non-trivial hint, a summary message should be injected
	size_t convo_size_before = agent->get_conversation().size();
	agent->inject_archived_episodes_summary();
	auto convo_with_summary = agent->get_conversation();
	assert(convo_with_summary.size() == convo_size_before + 1);
	bool found_summary_msg = false;
	for (const auto &msg : convo_with_summary) {
		if (msg.role == "system" && msg.content.find("[SYSTEM MEMORY: Archived Episodes Directory]") != std::string::npos) {
			found_summary_msg = true;
			assert(msg.content.find("agent_restore_context") != std::string::npos);
			assert(msg.content.find("episode_1") != std::string::npos);
			assert(msg.content.find("Resume when user asks about testing") != std::string::npos);
		}
	}
	assert(found_summary_msg);

	// If we make it trivial, no summary should be injected (size remains same)
	// First clear conversation to clean up the previous summary
	agent->set_conversation(convo_with_summary); // reset
	agent->update_episode_hint("episode_1", "Trivial or extremely brief episode.");
	// We clean up the injected summary from the conversation so we can verify no new summary is added
	auto cleaned_convo = agent->get_conversation();
	cleaned_convo.erase(std::remove_if(cleaned_convo.begin(), cleaned_convo.end(),
					   [](const message &m) {
						   return m.role == "system" &&
							  m.content.find("[SYSTEM MEMORY: Archived Episodes Directory]") !=
							      std::string::npos;
					   }),
			    cleaned_convo.end());
	agent->set_conversation(cleaned_convo);

	size_t size_before_trivial = agent->get_conversation().size();
	agent->inject_archived_episodes_summary();
	assert(agent->get_conversation().size() == size_before_trivial); // No summary added because all are trivial

	// Set hint back to normal
	agent->update_episode_hint("episode_1", "Resume when user asks about testing");

	std::cout << "\nTesting set_episode_state (paging in, shifting levels, and evicting)..." << std::endl;
	// Page out the turns to create episode_2
	agent->page_out_context(0, 2, "Manual Episode", "Manual Episode Summary", {"manual-tag"});

	auto convo_after_out = agent->get_conversation();
	bool found_anchor = false;
	for (const auto &msg : convo_after_out) {
		if (msg.role == "system" && msg.content.find("Raw history archive: episode_2") != std::string::npos) {
			found_anchor = true;
		}
	}
	assert(found_anchor);

	// Page in episode_2 at level 1
	bool pagein_ok = agent->set_episode_state("episode_2", 1);
	assert(pagein_ok);

	auto convo_after_in = agent->get_conversation();
	bool found_paged_in_turns = false;
	for (const auto &msg : convo_after_in) {
		if (msg.episode_id == "episode_2") {
			assert(msg.episode_level == 1);
			found_paged_in_turns = true;
		}
	}
	assert(found_paged_in_turns);

	// Shift level to level 2
	bool shift_ok = agent->set_episode_state("episode_2", 2);
	assert(shift_ok);

	auto convo_after_shift = agent->get_conversation();
	bool found_shifted_turns = false;
	for (const auto &msg : convo_after_shift) {
		if (msg.episode_id == "episode_2") {
			assert(msg.episode_level == 2);
			found_shifted_turns = true;
		}
	}
	assert(found_shifted_turns);

	// Evict (page out) episode_2
	bool evict_ok = agent->set_episode_state("episode_2", 99);
	assert(evict_ok);

	auto convo_after_evict = agent->get_conversation();
	bool found_anchor_again = false;
	for (const auto &msg : convo_after_evict) {
		// Turns should be gone (no active level 0-2 turns should remain; only the level 99 anchor is allowed)
		if (msg.episode_id == "episode_2") {
			assert(msg.episode_level == 99);
		}
		if (msg.role == "system" && msg.content.find("Raw history archive: episode_2") != std::string::npos) {
			found_anchor_again = true;
		}
	}
	assert(found_anchor_again);
	std::cout << "Episode state machine transitions verified successfully!" << std::endl;

	std::cout << "\nTesting compaction_engine planning logic..." << std::endl;
	std::vector<active_episode_info> candidates = {{"episode_1", 0, 10, 1000, 600, 400}, {"episode_2", 0, 20, 2000, 1200, 800}};

	auto planned = compaction_engine::plan_compaction(candidates, 3000, 3500);
	assert(planned.empty());

	planned = compaction_engine::plan_compaction(candidates, 3000, 2500);
	assert(planned.size() == 2);
	assert(planned[0].episode_id == "episode_1");
	assert(planned[0].target_level == 1);
	assert(planned[1].episode_id == "episode_1");
	assert(planned[1].target_level == 2);

	planned = compaction_engine::plan_compaction(candidates, 3000, 1000);
	assert(planned.size() > 2);
	assert(planned[0].episode_id == "episode_1");
	assert(planned[0].target_level == 1);
	std::cout << "Compaction engine planning logic verified successfully!" << std::endl;

	std::cout << "\nTesting tool call boundary protection during page_out_context..." << std::endl;
	{
		auto agent3 = ai_agent::create(3, "TestAgent3", model, &q, nullptr);
		std::vector<message> convo;
		// 0: system prompt
		message sys_msg;
		sys_msg.role = "system";
		sys_msg.content = "System prompt";
		convo.push_back(sys_msg);

		// 1: enter_plan_mode assistant message with tool call
		message ast_enter;
		ast_enter.role = "assistant";
		ast_enter.content = "Enter plan mode";
		tool_call tc_enter;
		tc_enter.id = "call_enter";
		tc_enter.function.name = "enter_plan_mode";
		tc_enter.function.arguments = "{}";
		ast_enter.tool_calls = {tc_enter};
		convo.push_back(ast_enter);

		// 2: enter_plan_mode tool response
		message tool_enter;
		tool_enter.role = "tool";
		tool_enter.tool_call_id = "call_enter";
		tool_enter.name = "enter_plan_mode";
		tool_enter.content = "Entered plan mode.";
		convo.push_back(tool_enter);

		// 3: user message inside plan mode
		message user_msg;
		user_msg.role = "user";
		user_msg.content = "Some research";
		convo.push_back(user_msg);

		// 4: exit_plan_mode assistant message with tool call
		message ast_exit;
		ast_exit.role = "assistant";
		ast_exit.content = "Exit plan mode";
		tool_call tc_exit;
		tc_exit.id = "call_exit";
		tc_exit.function.name = "exit_plan_mode";
		tc_exit.function.arguments = "{}";
		ast_exit.tool_calls = {tc_exit};
		convo.push_back(ast_exit);

		agent3->set_conversation(convo);

		// Page out context from index 2 to 5.
		// Index 2 is the tool response of enter_plan_mode (part of [1, 2]).
		// Index 4 is the assistant message of exit_plan_mode (part of [4, 5] since response is pending).
		agent3->page_out_context(2, 5, "Plan Archive", "Testing plan archiving", {"test"});

		auto resulting_convo = agent3->get_conversation();
		std::cout << "Resulting conversation size: " << resulting_convo.size() << std::endl;
		for (size_t i = 0; i < resulting_convo.size(); ++i) {
			std::cout << i << ": " << resulting_convo[i].role << " - " << resulting_convo[i].content.substr(0, 40) << std::endl;
		}

		assert(resulting_convo.size() == 5);
		assert(resulting_convo[1].role == "assistant");
		assert(resulting_convo[1].tool_calls && resulting_convo[1].tool_calls->at(0).id == "call_enter");
		assert(resulting_convo[2].role == "tool");
		assert(resulting_convo[2].tool_call_id == "call_enter");
		assert(resulting_convo[3].role == "system" && resulting_convo[3].content.find("Episode Archived") != std::string::npos);
		assert(resulting_convo[4].role == "assistant");
		assert(resulting_convo[4].tool_calls && resulting_convo[4].tool_calls->at(0).id == "call_exit");
		std::cout << "Tool call boundary protection verified successfully!" << std::endl;
	}

	std::cout << "\nTesting agent_add_todo..." << std::endl;
	{
		// Valid todo addition
		std::string add_todo_res = registry.execute_tool("agent_add_todo", "{\"text\": \"My valid todo\"}", ctx);
		std::cout << "Result: " << add_todo_res << std::endl;
		assert(add_todo_res.find("Added todo: My valid todo") != std::string::npos);

		// Rejection of empty text (validation fails)
		auto prep_todo = registry.prepare_tool("agent_add_todo", "{\"text\": \"\"}", ctx);
		assert(prep_todo.tool == nullptr);
		assert(!prep_todo.error_message.empty());

		// Rejection of overly long text (> 1024 characters)
		std::string long_text(1025, 'a');
		prep_todo = registry.prepare_tool("agent_add_todo", "{\"text\": \"" + long_text + "\"}", ctx);
		assert(prep_todo.tool == nullptr);
		assert(!prep_todo.error_message.empty());

		// Rejection of control characters (ESC \x1b)
		prep_todo = registry.prepare_tool("agent_add_todo", "{\"text\": \"unsafe\\u001btodo\"}", ctx);
		assert(prep_todo.tool == nullptr);
		assert(!prep_todo.error_message.empty());

		// Rejection if agent is read-only
		auto original_ro = agent->is_read_only();
		agent->set_read_only(true);
		prep_todo = registry.prepare_tool("agent_add_todo", "{\"text\": \"Valid but RO\"}", ctx);
		assert(prep_todo.tool == nullptr);
		assert(prep_todo.error_message.find("read-only") != std::string::npos);

		// Directly test validate_runtime on the tool under read-only state
		{
			tools::agent_add_todo_tool direct_tool("Valid but RO");
			std::string direct_err;
			assert(direct_tool.validate_runtime(ctx, direct_err) == false);
			assert(direct_err.find("read-only") != std::string::npos);
		}

		agent->set_read_only(original_ro);
		std::cout << "agent_add_todo tool verified successfully!" << std::endl;
	}

	std::cout << "\nTesting ai_agent::coalesce_tool_calls..." << std::endl;
	{
		std::vector<tool_call> calls;
		
		tool_call t1;
		t1.id = "call1";
		t1.type = "function";
		t1.function.name = "fs_read_lines";
		t1.function.arguments = "{\"path\": \"src/main.cpp\", \"start_line\": 10, \"end_line\": 20}";
		calls.push_back(t1);

		tool_call t2;
		t2.id = "call2";
		t2.type = "function";
		t2.function.name = "fs_read_lines";
		t2.function.arguments = "{\"path\": \"src/main.cpp\", \"start_line\": 25, \"end_line\": 40}";
		calls.push_back(t2);

		tool_call t3;
		t3.id = "call3";
		t3.type = "function";
		t3.function.name = "fs_read_lines";
		t3.function.arguments = "{\"path\": \"src/main.cpp\", \"start_line\": 100, \"end_line\": 120}";
		calls.push_back(t3);

		tool_call t4;
		t4.id = "call4";
		t4.type = "function";
		t4.function.name = "agent_add_todo";
		t4.function.arguments = "{\"text\": \"hello\"}";
		calls.push_back(t4);

		std::unordered_map<std::string, std::string> merged_to_parent;
		std::unordered_map<std::string, std::pair<int, int>> parent_ranges;
		ai_agent::coalesce_tool_calls(calls, merged_to_parent, parent_ranges);

		assert(merged_to_parent.size() == 1);
		assert(merged_to_parent["call2"] == "call1");
		
		assert(parent_ranges.size() == 2);
		assert(parent_ranges["call1"].first == 10);
		assert(parent_ranges["call1"].second == 40);
		assert(parent_ranges["call3"].first == 100);
		assert(parent_ranges["call3"].second == 120);

		auto arg1 = nlohmann::json::parse(calls[0].function.arguments);
		assert(arg1["start_line"] == 10);
		assert(arg1["end_line"] == 40);

		auto arg3 = nlohmann::json::parse(calls[2].function.arguments);
		assert(arg3["start_line"] == 100);
		assert(arg3["end_line"] == 120);

		std::cout << "ai_agent::coalesce_tool_calls verified successfully!" << std::endl;
	}

	std::cout << "\nAll test tools verified!" << std::endl;
	return 0;
}
