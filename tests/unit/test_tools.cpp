#include "test_watchdog.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <thread>
#include <chrono>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/compaction_engine.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/agentlib/virtual_file_system.h"
#include "../../src/images/image_manager.h"
#include "../../src/config_manager.h"
#include "../../src/event_queue.h"
#include "../../src/git_manager.h"
#include "../../src/project_manager.h"
#include "../../src/fs_utils.h"
#include "filter_registry.h"

using namespace agentlib;

int main()
{
	test_watchdog::setup_watchdog(30);
	// Initialize managers
	project_manager::get_instance().initialize();

extern std::string troff2md(std::string troff_content);

	filter_registry::get_instance().register_filter("troff_to_markdown", [](const std::string &input) {
		return troff2md(input);
	});

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;
	event_queue q;

	ctx.fs_security.set_working_directory(project_manager::get_instance().get_project_root());
	ctx.fs_security.add_allowed_root(project_manager::get_instance().get_project_root(), access_type::read);
	ctx.fs_security.add_allowed_root(project_manager::get_instance().get_project_root(), access_type::write);

	std::cout << "\nTesting fs_run_tests with specific tests..." << std::endl;
	// We'll run a fast test like unit_event_logger
	std::string run_result = registry.execute_tool("fs_run_tests", "{\"test_names\": [\"unit_event_logger\"]}", ctx);
	std::cout << "Result:\n" << run_result << std::endl;

	assert(run_result.find("meson test") != std::string::npos);
	assert(run_result.find("unit_event_logger") != std::string::npos);
#if !defined(__SANITIZE_ADDRESS__) && !(defined(__has_feature) && __has_feature(address_sanitizer))
	assert(run_result.find("OK") != std::string::npos || run_result.find("PASS") != std::string::npos ||
	       run_result.find("exit status 0") != std::string::npos);
#endif

	std::cout << "\nTesting fs_run_tests with space-containing test name..." << std::endl;
	std::string space_run_result = registry.execute_tool("fs_run_tests", "{\"test_names\": [\"test with space\"]}", ctx);
	std::cout << "Space run result:\n" << space_run_result << std::endl;
	assert(space_run_result.find("'test with space'") != std::string::npos ||
	       space_run_result.find("\"test with space\"") != std::string::npos);

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

		// 1: tool_a assistant message with tool call
		message ast_enter;
		ast_enter.role = "assistant";
		ast_enter.content = "Call tool a";
		tool_call tc_enter;
		tc_enter.id = "call_enter";
		tc_enter.function.name = "tool_a";
		tc_enter.function.arguments = "{}";
		ast_enter.tool_calls = {tc_enter};
		convo.push_back(ast_enter);

		// 2: tool_a tool response
		message tool_enter;
		tool_enter.role = "tool";
		tool_enter.tool_call_id = "call_enter";
		tool_enter.name = "tool_a";
		tool_enter.content = "Output a.";
		convo.push_back(tool_enter);

		// 3: user message inside exploration
		message user_msg;
		user_msg.role = "user";
		user_msg.content = "Some research";
		convo.push_back(user_msg);

		// 4: tool_b assistant message with tool call
		message ast_exit;
		ast_exit.role = "assistant";
		ast_exit.content = "Call tool b";
		tool_call tc_exit;
		tc_exit.id = "call_exit";
		tc_exit.function.name = "tool_b";
		tc_exit.function.arguments = "{}";
		ast_exit.tool_calls = {tc_exit};
		convo.push_back(ast_exit);

		agent3->set_conversation(convo);

		// Page out context from index 2 to 5.
		agent3->page_out_context(2, 5, "Plan Archive", "Testing plan archiving", {"test"});

		auto resulting_convo = agent3->get_conversation();
		std::cout << "Resulting conversation size: " << resulting_convo.size() << std::endl;
		for (size_t i = 0; i < resulting_convo.size(); ++i) {
			std::cout << i << ": " << resulting_convo[i].role << " - " << resulting_convo[i].content.substr(0, 40) << std::endl;
		}

		assert(resulting_convo.size() == 5);
		assert(resulting_convo[0].role == "system" && resulting_convo[0].content.find("Episode Archived") != std::string::npos);
		assert(resulting_convo[1].role == "system" && resulting_convo[1].content == "System prompt");
		assert(resulting_convo[2].role == "assistant");
		assert(resulting_convo[2].tool_calls && resulting_convo[2].tool_calls->at(0).id == "call_enter");
		assert(resulting_convo[3].role == "tool");
		assert(resulting_convo[3].tool_call_id == "call_enter");
		assert(resulting_convo[4].role == "assistant");
		assert(resulting_convo[4].tool_calls && resulting_convo[4].tool_calls->at(0).id == "call_exit");
		std::cout << "Tool call boundary protection verified successfully!" << std::endl;
	}



	std::cout << "\nTesting fs_read_lines boundary heuristics..." << std::endl;
	{
		std::string proj_root = project_manager::get_instance().get_project_root();
		std::filesystem::path temp_file_path = std::filesystem::path(proj_root) / "test_fs_read_lines_heuristics.py";
		std::string temp_file = "test_fs_read_lines_heuristics.py";
		{
			std::ofstream out(temp_file_path);
			out << "line 1\n"
			    << "line 2\n"
			    << "line 3\n"
			    << "line 4\n"
			    << "line 5\n"
			    << "line 6\n"
			    << "\n"
			    << "def func():\n"
			    << "    pass\n"
			    << "line 10\n";
		}

		ctx.fs_security.add_allowed_root(".", access_type::read);

		{
			std::string args = "{\"path\": \"" + temp_file + "\", \"start_line\": 1, \"end_line\": 4}";
			std::string res = registry.execute_tool("fs_read_lines", args, ctx);

			assert(res.find("Code for lines 1 - 10 of " + temp_file + " (total 10 lines):") != std::string::npos);
			assert(res.find("1: line 1") != std::string::npos);
			assert(res.find("10: line 10") != std::string::npos);
		}

		{
			std::ofstream out(temp_file_path);
			for (int i = 1; i <= 100; ++i) {
				if (i == 45) {
					out << "\n";
				} else if (i == 46) {
					out << "def next_func():\n";
				} else if (i >= 41 && i <= 44) {
					out << "    line " << i << "\n";
				} else {
					out << "line " << i << "\n";
				}
			}
		}

		{
			std::string args = "{\"path\": \"" + temp_file + "\", \"start_line\": 1, \"end_line\": 40}";
			std::string res = registry.execute_tool("fs_read_lines", args, ctx);

			assert(res.find("Code for lines 1 - 45 of " + temp_file + " (total 100 lines):") != std::string::npos);
			assert(res.find("40: line 40") != std::string::npos);
			assert(res.find("44:     line 44") != std::string::npos);
			assert(res.find("46: def next_func():") == std::string::npos);
		}

		std::filesystem::path temp_cpp_path = std::filesystem::path(proj_root) / "test_fs_read_lines_heuristics.cpp";
		std::string temp_cpp = "test_fs_read_lines_heuristics.cpp";
		{
			std::ofstream out(temp_cpp_path);
			for (int i = 1; i <= 100; ++i) {
				if (i == 45) {
					out << "}\n";
				} else {
					out << "line " << i << "\n";
				}
			}
		}

		{
			std::string args = "{\"path\": \"" + temp_cpp + "\", \"start_line\": 1, \"end_line\": 40}";
			std::string res = registry.execute_tool("fs_read_lines", args, ctx);
			assert(res.find("Code for lines 1 - 45 of " + temp_cpp + " (total 100 lines):") != std::string::npos);
			assert(res.find("45: }") != std::string::npos);
			assert(res.find("46:") == std::string::npos);
		}

		{
			std::ofstream out(temp_file_path);
			out << "one\ntwo\nthree\nfour\nfive\nsix\nseven\neight\nnine\nten\n";
		}

		// Test tail success case (get last 3 lines)
		{
			std::string args = "{\"path\": \"" + temp_file + "\", \"tail\": 3}";
			std::string res = registry.execute_tool("fs_read_lines", args, ctx);
			assert(res.find("8: eight") != std::string::npos);
			assert(res.find("9: nine") != std::string::npos);
			assert(res.find("10: ten") != std::string::npos);
			assert(res.find("7: seven") == std::string::npos);
		}

		// Test tail validation failure (mixing tail with start_line)
		{
			std::string args = "{\"path\": \"" + temp_file + "\", \"start_line\": 1, \"tail\": 3}";
			auto prep = registry.prepare_tool("fs_read_lines", args, ctx);
			assert(prep.tool == nullptr);
			assert(!prep.error_message.empty());
		}

		std::filesystem::remove(temp_file_path);
		std::filesystem::remove(temp_cpp_path);
		std::cout << "fs_read_lines boundary heuristics verified successfully!" << std::endl;
	}

	std::cout << "\nTesting fs_read_symbol..." << std::endl;
	{
		std::string proj_root = project_manager::get_instance().get_project_root();
		std::filesystem::path temp_file_path = std::filesystem::path(proj_root) / "test_fs_read_symbol_heuristics.cpp";
		std::string temp_file = "test_fs_read_symbol_heuristics.cpp";
		{
			std::ofstream out(temp_file_path);
			out << "int _my_unit_test_function(int x, int y) {\n"
			    << "    return x + y;\n"
			    << "}\n"
			    << "int my_unit_test_function(int x, int y) {\n"
			    << "    return x * y;\n"
			    << "}\n";
		}

		ctx.fs_security.add_allowed_root(".", access_type::read);

		event_queue dummy_queue;
		project_manager::get_instance().lsp_start(dummy_queue);

		// Wait a bit for clangd to spawn/initialize
		std::this_thread::sleep_for(std::chrono::milliseconds(800));

		{
			std::string args = "{\"path\": \"" + temp_file + "\", \"symbol_name\": \"my_unit_test_function\"}";
			std::string res = registry.execute_tool("fs_read_symbol", args, ctx);
			std::cout << "fs_read_symbol result:\n" << res << std::endl;

			assert(res.find("my_unit_test_function") != std::string::npos);
			assert(res.find("StartLine: 4") != std::string::npos);
			assert(res.find("EndLine: 6") != std::string::npos);
			assert(res.find("4: int my_unit_test_function") != std::string::npos);
			assert(res.find("5:     return x * y;") != std::string::npos);
		}

		project_manager::get_instance().lsp_stop();
		std::filesystem::remove(temp_file_path);
		std::cout << "fs_read_symbol verified successfully!" << std::endl;
	}

	{
		std::cout << "\nTesting apply_text_filter..." << std::endl;

		// Test 1: Simple filter execution returning value
		std::string result = registry.execute_tool(
			"apply_text_filter",
			"{\"text\": \"\\u001b[31mHello\\u001b[0m World\", \"filter\": \"strip_ansi\"}",
			ctx
		);
		std::cout << "Result: " << result << std::endl;
		assert(result == "Hello World");

		// Test 1b: Simple filter execution with literal \x1b and \u001b
		std::string result_literal = registry.execute_tool(
			"apply_text_filter",
			"{\"text\": \"\\\\x1b[32mThis is green text\\\\x1b[0m and \\\\u001b[31mred text\\\\u001b[0m\", \"filter\": \"strip_ansi\"}",
			ctx
		);
		std::cout << "Result Literal: " << result_literal << std::endl;
		assert(result_literal == "This is green text and red text");

		// Test 2: Filter execution writing to output_path
		std::string output_file = "test_filtered_out.txt";
		std::string result_write = registry.execute_tool(
			"apply_text_filter",
			"{\"text\": \"\\u001b[31mHello\\u001b[0m World\", \"filter\": \"strip_ansi\", \"output_path\": \"" + output_file + "\"}",
			ctx
		);
		std::cout << "Write result: " << result_write << std::endl;
		assert(result_write.find("Successfully applied filter") != std::string::npos);

		std::string full_out_path = (ctx.fs_security.get_working_directory() / output_file).string();
		assert(std::filesystem::exists(full_out_path));

		std::ifstream ifs(full_out_path);
		std::string file_content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
		assert(file_content == "Hello World");
		ifs.close();
		std::filesystem::remove(full_out_path);

		// Test 3: Invalid filter name error validation
		auto prep_invalid_filter = registry.prepare_tool(
			"apply_text_filter",
			"{\"text\": \"Hello\", \"filter\": \"nonexistent_filter_name_xyz\"}",
			ctx
		);
		assert(prep_invalid_filter.tool == nullptr);
		std::cout << "Invalid filter error: " << prep_invalid_filter.error_message << std::endl;
		assert(prep_invalid_filter.error_message.find("Invalid filter name") != std::string::npos);
		assert(prep_invalid_filter.error_message.find("strip_ansi") != std::string::npos); // Should list available filters

		// Test 4: Security violation on write path
		auto prep_security = registry.prepare_tool(
			"apply_text_filter",
			"{\"text\": \"Hello\", \"filter\": \"strip_ansi\", \"output_path\": \"../../unsafe.txt\"}",
			ctx
		);
		assert(prep_security.tool == nullptr);
		assert(prep_security.error_message.find("Security Violation") != std::string::npos ||
		       prep_security.error_message.find("Access Denied") != std::string::npos ||
		       prep_security.error_message.find("outside workspace") != std::string::npos);

		// Test 5: troff_to_markdown filter
		{
			std::string result_troff = registry.execute_tool(
				"apply_text_filter",
				"{\"text\": \".SH Header\\nSome\\n.B \\\"bold text.\\\"\", \"filter\": \"troff_to_markdown\"}",
				ctx
			);
			std::cout << "Troff result:\n" << result_troff << std::endl;
			assert(result_troff.find("# Header") != std::string::npos);
			assert(result_troff.find("**bold text.**") != std::string::npos);
		}

		// Test 6: troff_to_markdown filter with code blocks suppressing bold/italic
		{
			std::string result_troff = registry.execute_tool(
				"apply_text_filter",
				"{\"text\": \".nf\\nSome .B \\\"bold text\\\" and \\\\fBfont bold\\\\fR\\n.fi\", \"filter\": \"troff_to_markdown\"}",
				ctx
			);
			std::cout << "Troff code block result:\n" << result_troff << std::endl;
			assert(result_troff.find("```c") != std::string::npos);
			assert(result_troff.find("bold text") != std::string::npos);
			assert(result_troff.find("**bold text**") == std::string::npos);
		}

		// Test 7: apply_text_filter reading from input path parameter
		{
			std::string in_file = "test_filter_in.txt";
			std::string full_in_path = (ctx.fs_security.get_working_directory() / in_file).string();
			std::ofstream ofs(full_in_path, std::ios::binary);
			ofs << "\x1b[32mPath input test\x1b[0m";
			ofs.close();

			std::string out_file = "test_filter_in_out.txt";
			std::string result_path = registry.execute_tool(
				"apply_text_filter",
				"{\"path\": \"" + in_file + "\", \"filter\": \"strip_ansi\", \"output_path\": \"" + out_file + "\"}",
				ctx
			);
			std::cout << "Path filter result: " << result_path << std::endl;
			assert(result_path.find("Successfully applied filter") != std::string::npos);

			std::string full_out_path = (ctx.fs_security.get_working_directory() / out_file).string();
			assert(std::filesystem::exists(full_out_path));

			std::ifstream ifs(full_out_path);
			std::string file_content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
			assert(file_content == "Path input test");
			ifs.close();

			std::filesystem::remove(full_in_path);
			std::filesystem::remove(full_out_path);
		}
	}

	std::cout << "\nTesting fs_write_file VFS integration..." << std::endl;
	{
		std::string proj_root = project_manager::get_instance().get_project_root();
		std::string target_file = proj_root + "/test_fs_write_vfs.txt";
		std::string target_uri = "file://" + target_file;

		// Initialize virtual file system and attach to security manager
		virtual_file_system vfs;
		ctx.fs_security.set_vfs(&vfs);

		// Write via tool execution
		std::string args = "{\"path\": \"" + target_uri + "\", \"content\": \"VFS tool write!\", \"append\": false}";
		std::string res = registry.execute_tool("fs_write_file", args, ctx);
		assert(res.find("Successfully wrote") != std::string::npos);

		// Verify on-disk file was created
		assert(std::filesystem::exists(target_file));
		std::ifstream ifs(target_file);
		std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
		assert(content == "VFS tool write!");
		ifs.close();

		// Append via tool execution
		std::string append_args = "{\"path\": \"" + target_uri + "\", \"content\": \"Appended.\", \"append\": true}";
		std::string append_res = registry.execute_tool("fs_write_file", append_args, ctx);
		assert(append_res.find("Successfully wrote") != std::string::npos);

		std::ifstream ifs2(target_file);
		std::string content2((std::istreambuf_iterator<char>(ifs2)), std::istreambuf_iterator<char>());
		assert(content2 == "VFS tool write!\nAppended.");
		ifs2.close();

		std::filesystem::remove(target_file);

		// Test tmp:// VFS scheme
		std::string tmp_uri = "tmp://000_test_fs_write_tmp.txt";
		std::string tmp_args = "{\"path\": \"" + tmp_uri + "\", \"content\": \"Temporary VFS write!\", \"append\": false}";
		std::string tmp_res = registry.execute_tool("fs_write_file", tmp_args, ctx);
		assert(tmp_res.find("Successfully wrote") != std::string::npos);

		// Verify on-disk file in temp dir
		std::string expected_tmp_file = fs_utils::get_project_tmp_dir() + "/000_test_fs_write_tmp.txt";
		assert(std::filesystem::exists(expected_tmp_file));
		std::ifstream ifs_tmp(expected_tmp_file);
		std::string content_tmp((std::istreambuf_iterator<char>(ifs_tmp)), std::istreambuf_iterator<char>());
		assert(content_tmp == "Temporary VFS write!");
		ifs_tmp.close();

		// Test tmp:// directory listing
		std::string list_args = "{\"path\": \"tmp://\", \"rich_metadata\": false}";
		std::string list_res = registry.execute_tool("fs_list_dir", list_args, ctx);
		assert(list_res.find("000_test_fs_write_tmp.txt") != std::string::npos);

		std::filesystem::remove(expected_tmp_file);

		// Test fs_purge_tmp tool
		std::string purge_file1 = "tmp://test_unique_purge_task_1.txt";
		std::string purge_file2 = "tmp://test_keep_task_2.txt";
		std::string purge_file3 = "tmp://test_unique_purge_task_3.txt";

		registry.execute_tool("fs_write_file", "{\"path\": \"" + purge_file1 + "\", \"content\": \"purge me!\", \"append\": false}", ctx);
		registry.execute_tool("fs_write_file", "{\"path\": \"" + purge_file2 + "\", \"content\": \"keep me!\", \"append\": false}", ctx);
		registry.execute_tool("fs_write_file", "{\"path\": \"" + purge_file3 + "\", \"content\": \"purge me!\", \"append\": false}", ctx);

		assert(vfs.exists(purge_file1));
		assert(vfs.exists(purge_file2));
		assert(vfs.exists(purge_file3));

		// Purge with substring "unique_purge_task"
		std::string purge_res1 = registry.execute_tool("fs_purge_tmp", "{\"substring\": \"unique_purge_task\"}", ctx);
		assert(purge_res1.find("Successfully purged 2 files") != std::string::npos);
		assert(!vfs.exists(purge_file1));
		assert(vfs.exists(purge_file2));
		assert(!vfs.exists(purge_file3));

		// Purge everything remaining
		std::string purge_res2 = registry.execute_tool("fs_purge_tmp", "{}", ctx);
		assert(!vfs.exists(purge_file2));

		// Test fs_mkdir in tmp://
		std::string vfs_dir = "tmp://nested_test_dir/sub_dir";
		std::string mkdir_args = "{\"path\": \"" + vfs_dir + "\"}";
		std::string mkdir_res = registry.execute_tool("fs_mkdir", mkdir_args, ctx);
		assert(mkdir_res.find("Successfully created directory in virtual file system") != std::string::npos);

		std::string vfs_file = vfs_dir + "/test_file.txt";
		std::string write_args = "{\"path\": \"" + vfs_file + "\", \"content\": \"VFS nested file content\", \"append\": false}";
		std::string write_res = registry.execute_tool("fs_write_file", write_args, ctx);
		assert(write_res.find("Successfully wrote") != std::string::npos);
		assert(vfs.exists(vfs_file));

		// Cleanup via purge
		registry.execute_tool("fs_purge_tmp", "{}", ctx);
		assert(!vfs.exists(vfs_file));

		// Test images:// VFS path tool execution
		images::image_manager::get_instance().initialize();
		std::string dummy_img_content = "VFS image mock text content";

		std::string image_uri = "images://by-name/tool-test-image.png";
		std::string img_args = "{\"path\": \"" + image_uri + "\", \"content\": \"" + dummy_img_content + "\", \"append\": false}";
		std::string img_res = registry.execute_tool("fs_write_file", img_args, ctx);
		assert(img_res.find("Successfully decompressed and ingested image") != std::string::npos);
		assert(vfs.exists(image_uri));

		// Cleanup
		images::image_manager::get_instance().delete_image(image_uri);
		ctx.fs_security.set_vfs(nullptr);
	}

	std::cout << "\nTesting read-only agent tool purity enforcement..." << std::endl;
	{
		tool_context ro_ctx = ctx;
		ro_ctx.properties.read_only = true;

		auto find_val = [&](const std::string &name) -> std::shared_ptr<tool_validator> {
			for (const auto &v : registry.get_all_registered_validators()) {
				if (v->get_name() == name) return v;
			}
			return nullptr;
		};

		// Pure tools MUST succeed for read-only agents
		auto val_timer = find_val("agent_set_timer");
		assert(val_timer && val_timer->is_pure());

		auto val_read = find_val("fs_read_lines");
		assert(val_read && val_read->is_pure());

		auto val_list = find_val("fs_list_dir");
		assert(val_list && val_list->is_pure());

		// Dynamic is_pure(args) for fs_write_file
		auto val_write = find_val("fs_write_file");
		assert(val_write != nullptr);
		assert(!val_write->is_pure(nlohmann::json{{"path", "src/main.cpp"}}));
		assert(val_write->is_pure(nlohmann::json{{"path", "tmp://scratch.txt"}}));
		assert(val_write->is_pure(nlohmann::json{{"path", "images://test.png"}}));

		// Writing to workspace file as read-only MUST be blocked
		std::string ro_write_res = registry.execute_tool("fs_write_file", "{\"path\": \"src/main.cpp\", \"content\": \"test\"}", ro_ctx);
		assert(ro_write_res.find("Security Violation: Agent is in read-only mode") != std::string::npos);
	}

	std::cout << "\nAll test tools verified!" << std::endl;
	return 0;
}
