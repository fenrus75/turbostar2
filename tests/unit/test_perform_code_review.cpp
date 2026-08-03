#include "test_watchdog.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <csignal>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/subagent_manager.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/codereview_manager.h"
#include "../../src/event_queue.h"
#include "../../src/fs_utils.h"
#include "../../src/project_manager.h"

using namespace agentlib;

void test_perform_code_review_execution()
{
	std::cout << "Testing perform_code_review tool..." << std::endl;

	std::filesystem::path temp_proj = std::filesystem::temp_directory_path() / ("test_review_proj_" + std::to_string(getpid()));
	std::filesystem::remove_all(temp_proj);
	std::filesystem::create_directories(temp_proj);

	// Create a single test file we can review
	std::filesystem::path test_file_path = temp_proj / "src_to_review.cpp";
	{
		std::ofstream ofs(test_file_path);
		ofs << "int main() {\n\tint* p = nullptr;\n\t*p = 42;\n\treturn 0;\n}\n";
	}

	fs_utils::set_override_project_dir(temp_proj.string());
	setenv("TURBOSTAR_PROJECT_ROOT", temp_proj.string().c_str(), 1);
	project_manager::get_instance().initialize();
	codereview_manager::get_instance().load_project(temp_proj.string());
	codereview_manager::get_instance().clear_all();

	// Pre-create a code review item for this file to test prompt injection
	codereview_manager::get_instance().create_code_review_item("Old Null Pointer", "src_to_review.cpp", 2, "int* p = nullptr;", "high",
								   "Deref", "Fix");

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;
	event_queue q;

	ctx.fs_security.set_working_directory(temp_proj.string());
	ctx.fs_security.add_allowed_root(temp_proj.string(), access_type::read);
	ctx.fs_security.add_allowed_root(temp_proj.string(), access_type::write);
	ctx.queue = &q;

	auto model = std::make_shared<ai_model>("test-model", "Test Model", "http://localhost", "Test", 0.0, 0.0);
	auto agent = ai_agent::create(98001, "DeveloperAgent", model, &q, nullptr);
	agent->set_role(agent_role::developer);
	ctx.active_agent = agent.get();
	ctx.properties = agent->get_properties();

	// Test 1: Role gating (reviewer role should be blocked from calling this tool to avoid recursion)
	auto reviewer_temp = ai_agent::create(98002, "TempReviewer", model, &q, nullptr);
	reviewer_temp->set_role(agent_role::reviewer);
	tool_context reviewer_ctx = ctx;
	reviewer_ctx.active_agent = reviewer_temp.get();
	reviewer_ctx.properties = reviewer_temp->get_properties();

	std::string args_json = "{\"files\": [\"src_to_review.cpp\"], \"instructions\": \"Check for bugs\", \"async\": true}";
	auto prep = registry.prepare_tool("perform_code_review", args_json, reviewer_ctx);
	assert(prep.tool == nullptr);
	assert(!prep.error_message.empty());

	// Test 2: Developer role calls it (allowed)
	prep = registry.prepare_tool("perform_code_review", args_json, ctx);
	assert(prep.tool != nullptr);

	std::string res_str = registry.execute_tool("perform_code_review", args_json, ctx);
	std::cout << "Result: " << res_str << std::endl;
	assert(res_str.find("Code review started asynchronously") != std::string::npos);

	// Verify subagent was created
	auto subagents = agent->get_subagents();
	assert(!subagents.empty());
	auto subagent = subagents[0];
	assert(subagent->get_role() == agent_role::reviewer);
	assert(subagent->get_name() == "Code Reviewer");

	// Verify previous code reviews and file contents were injected in the system prompt
	auto sub_convo = subagent->get_conversation();
	assert(!sub_convo.empty());

	bool found_old_null_pointer = false;

	for (const auto &msg : sub_convo) {
		if (msg.content.find("Old Null Pointer") != std::string::npos) {
			found_old_null_pointer = true;
		}
	}

	assert(found_old_null_pointer);

	// Test 3: Sandbox write-access constraints on the reviewer agent
	// Prepare tool context for the reviewer subagent
	tool_context sub_ctx;
	sub_ctx.active_agent = subagent.get();
	std::filesystem::path workspace_root(temp_proj.string());
	sub_ctx.fs_security.set_working_directory(workspace_root);
	sub_ctx.fs_security.add_allowed_root(workspace_root, access_type::read);

	// reviewer agent with no allowed_write_file_ should have no write access at all
	subagent->set_allowed_write_file("");
	std::string resolved_path, out_err;
	bool can_write = sub_ctx.fs_security.validate_access("report.md", access_type::write, resolved_path, out_err);
	assert(!can_write);

	// reviewer agent with allowed_write_file_ set to report.md should only write to report.md
	subagent->set_allowed_write_file("report.md");
	sub_ctx.fs_security.add_allowed_file(workspace_root / "report.md", access_type::write);

	can_write = sub_ctx.fs_security.validate_access("report.md", access_type::write, resolved_path, out_err);
	assert(can_write);

	can_write = sub_ctx.fs_security.validate_access("other_file.cpp", access_type::write, resolved_path, out_err);
	assert(!can_write);

	// Cancel and wait for background threads to exit to prevent stack-use-after-return
	subagent->cancel_current_task();
	subagent->set_status(agent_status::error);
	subagent->wait_until_idle();
	agent->cancel_current_task();
	agent->set_status(agent_status::dead);
	agent->wait_until_idle();

	// Cleanup
	fs_utils::set_override_project_dir("");
	unsetenv("TURBOSTAR_PROJECT_ROOT");
	project_manager::get_instance().initialize();
	std::filesystem::remove_all(temp_proj);
	std::cout << "perform_code_review tool and sandbox constraints verified successfully!" << std::endl;
}

void test_perform_code_review_splitting()
{
	std::cout << "Testing perform_code_review file splitting..." << std::endl;

	std::filesystem::path temp_proj = std::filesystem::temp_directory_path() / ("test_split_proj_" + std::to_string(getpid()));
	std::filesystem::remove_all(temp_proj);
	std::filesystem::create_directories(temp_proj);

	// Scenario A: Split by lines (> 1500 lines)
	// file1.cpp: 10 lines
	// file2.cpp: 1600 lines
	// file3.cpp: 50 lines
	// file4.cpp: 10 lines
	{
		std::ofstream ofs(temp_proj / "file1.cpp");
		for (int i = 0; i < 10; ++i) ofs << "line\n";
	}
	{
		std::ofstream ofs(temp_proj / "file2.cpp");
		for (int i = 0; i < 1600; ++i) ofs << "line\n";
	}
	{
		std::ofstream ofs(temp_proj / "file3.cpp");
		for (int i = 0; i < 50; ++i) ofs << "line\n";
	}
	{
		std::ofstream ofs(temp_proj / "file4.cpp");
		for (int i = 0; i < 10; ++i) ofs << "line\n";
	}

	// Scenario B: Split by count (> 10 files)
	// 12 small files: split_01.cpp to split_12.cpp (each 1 line)
	for (int i = 1; i <= 12; ++i) {
		std::ofstream ofs(temp_proj / std::format("split_{:02d}.cpp", i));
		ofs << "line\n";
	}

	fs_utils::set_override_project_dir(temp_proj.string());
	setenv("TURBOSTAR_PROJECT_ROOT", temp_proj.string().c_str(), 1);
	project_manager::get_instance().initialize();
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

	// Test Scenario A: Split by lines (expecting 3 subagents)
	{
		auto agent = ai_agent::create(98100, "DeveloperAgentA", model, &q, nullptr);
		agent->set_role(agent_role::developer);
		tool_context ctx_a = ctx;
		ctx_a.active_agent = agent.get();
		ctx_a.properties = agent->get_properties();

		std::string args_json = "{\"files\": [\"file1.cpp\", \"file2.cpp\", \"file3.cpp\", \"file4.cpp\"], \"async\": true}";
		auto prep = registry.prepare_tool("perform_code_review", args_json, ctx_a);
		assert(prep.tool != nullptr);

		std::string res_str = registry.execute_tool("perform_code_review", args_json, ctx_a);
		std::cout << "Scenario A Result: " << res_str << std::endl;

		auto subagents = agent->get_subagents();
		assert(subagents.size() == 3);
		assert(subagents[0]->get_name() == "Code Reviewer (Group 1)");
		assert(subagents[1]->get_name() == "Code Reviewer (Group 2)");
		assert(subagents[2]->get_name() == "Code Reviewer (Group 3)");

		// Check file grouping in prompts
		// Group 1 should contain file1.cpp
		// Group 2 should contain file2.cpp
		// Group 3 should contain file3.cpp and file4.cpp
		bool found_f1 = false, found_f2 = false, found_f3 = false, found_f4 = false;
		for (const auto &msg : subagents[0]->get_conversation()) {
			if (msg.content.find("file1.cpp") != std::string::npos) found_f1 = true;
		}
		for (const auto &msg : subagents[1]->get_conversation()) {
			if (msg.content.find("file2.cpp") != std::string::npos) found_f2 = true;
		}
		for (const auto &msg : subagents[2]->get_conversation()) {
			if (msg.content.find("file3.cpp") != std::string::npos) found_f3 = true;
			if (msg.content.find("file4.cpp") != std::string::npos) found_f4 = true;
		}
		assert(found_f1);
		assert(found_f2);
		assert(found_f3);
		assert(found_f4);

		for (auto &sa : subagents) {
			sa->cancel_current_task();
			sa->set_status(agent_status::error);
			sa->wait_until_idle();
		}
		agent->cancel_current_task();
		agent->set_status(agent_status::dead);
		agent->wait_until_idle();
	}

	// Test Scenario B: Split by count (expecting 2 subagents)
	{
		auto agent = ai_agent::create(98200, "DeveloperAgentB", model, &q, nullptr);
		agent->set_role(agent_role::developer);
		tool_context ctx_b = ctx;
		ctx_b.active_agent = agent.get();
		ctx_b.properties = agent->get_properties();

		std::vector<std::string> files_list;
		for (int i = 1; i <= 12; ++i) {
			files_list.push_back(std::format("split_{:02d}.cpp", i));
		}
		nlohmann::json raw_args = {{"files", files_list}, {"async", true}};
		std::string args_json = raw_args.dump();

		auto prep = registry.prepare_tool("perform_code_review", args_json, ctx_b);
		assert(prep.tool != nullptr);

		std::string res_str = registry.execute_tool("perform_code_review", args_json, ctx_b);
		std::cout << "Scenario B Result: " << res_str << std::endl;

		auto subagents = agent->get_subagents();
		assert(subagents.size() == 2);
		assert(subagents[0]->get_name() == "Code Reviewer (Group 1)");
		assert(subagents[1]->get_name() == "Code Reviewer (Group 2)");

		// Check grouping: Group 1 has 10 files, Group 2 has 2 files
		std::set<std::string> unique_files_g1;
		std::set<std::string> unique_files_g2;
		for (int i = 1; i <= 12; ++i) {
			std::string fname = std::format("split_{:02d}.cpp", i);
			for (const auto &msg : subagents[0]->get_conversation()) {
				if (msg.content.find(fname) != std::string::npos) {
					unique_files_g1.insert(fname);
				}
			}
			for (const auto &msg : subagents[1]->get_conversation()) {
				if (msg.content.find(fname) != std::string::npos) {
					unique_files_g2.insert(fname);
				}
			}
		}
		assert(unique_files_g1.size() == 10);
		assert(unique_files_g2.size() == 2);

		for (auto &sa : subagents) {
			sa->cancel_current_task();
			sa->set_status(agent_status::error);
			sa->wait_until_idle();
		}
		agent->cancel_current_task();
		agent->set_status(agent_status::dead);
		agent->wait_until_idle();
	}

	// Cleanup
	fs_utils::set_override_project_dir("");
	unsetenv("TURBOSTAR_PROJECT_ROOT");
	project_manager::get_instance().initialize();
	std::filesystem::remove_all(temp_proj);
	std::cout << "perform_code_review file splitting verified successfully!" << std::endl;
}

int main()
{
#ifndef _WIN32
	signal(SIGPIPE, SIG_IGN);
#endif
	test_watchdog::setup_watchdog(30);
	test_watchdog::scoped_test_home home_guard("perform_code_review");
	test_watchdog::init_plugin_environment();
	project_manager::get_instance().initialize();
	test_perform_code_review_execution();
	test_perform_code_review_splitting();
	return 0;
}
