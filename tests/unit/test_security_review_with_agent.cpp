#include "test_watchdog.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/codereview_manager.h"
#include "../../src/config_manager.h"
#include "../../src/event_queue.h"
#include "../../src/fs_utils.h"
#include "../../src/project_manager.h"

using namespace agentlib;

extern "C" void register_security_review_with_agent(void);

void test_security_review_with_agent_execution()
{
	register_security_review_with_agent();
	std::cout << "Testing security_review_with_agent tool..." << std::endl;

	std::filesystem::path orig_path = std::filesystem::current_path();
	std::filesystem::path temp_proj = orig_path / "test_temp_sec_review_proj";
	std::filesystem::remove_all(temp_proj);
	std::filesystem::create_directories(temp_proj);

	// Create a single test file we can review
	std::filesystem::path test_file_path = temp_proj / "sec_src_to_review.cpp";
	{
		std::ofstream ofs(test_file_path);
		ofs << "int main() {\n\tchar buf[8];\n\tstrcpy(buf, \"too long string!\");\n\treturn 0;\n}\n";
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

	auto model = std::make_shared<ai_model>("test-model", "Test Model", "http://localhost:1", "Test", 0.0, 0.0);
	ai_model_registry::get_instance().register_model(model);
	config_manager::get_instance().set_task_model_id("code_reviewer", "test-model");

	auto agent = ai_agent::create(1, "DeveloperAgent", model, &q, nullptr);
	agent->set_role(agent_role::developer);
	ctx.active_agent = agent.get();
	ctx.properties = agent->get_properties();

	// Test 1: Role gating (reviewer role should be blocked from calling this tool to avoid recursion)
	auto reviewer_temp = ai_agent::create(2, "TempReviewer", model, &q, nullptr);
	reviewer_temp->set_role(agent_role::reviewer);
	tool_context reviewer_ctx = ctx;
	reviewer_ctx.active_agent = reviewer_temp.get();
	reviewer_ctx.properties = reviewer_temp->get_properties();

	std::string args_json = "{\"files\": [\"sec_src_to_review.cpp\"], \"instructions\": \"Focus on buffer overflows\", \"result_file\": \"findings.md\"}";
	auto prep = registry.prepare_tool("security_review_with_agent", args_json, reviewer_ctx);
	assert(prep.tool == nullptr);
	assert(!prep.error_message.empty());

	// Test 2: Developer role calls it (allowed)
	prep = registry.prepare_tool("security_review_with_agent", args_json, ctx);
	assert(prep.tool != nullptr);

	// Since we are not running a real LLM server, the execute call will start, wait for subagent.
	// But the subagent will be idle (as we don't process it in a background thread in this unit test or we let wait_until_idle finish immediately).
	// Let's call execute, it should spawn the subagent.
	std::string res_str = registry.execute_tool("security_review_with_agent", args_json, ctx);
	std::cout << "Result: " << res_str << std::endl;

	// Verify subagent was created
	auto subagents = agent->get_subagents();
	assert(!subagents.empty());
	auto subagent = subagents[0];
	assert(subagent->get_role() == agent_role::reviewer);
	assert(subagent->get_name() == "Security Reviewer");

	// Verify it has the :plugin:securityagent tool family active
	assert(subagent->is_tool_family_active(":plugin:securityagent"));

	// Verify system prompt contents
	auto sub_convo = subagent->get_conversation();
	assert(!sub_convo.empty());

	bool found_buffer_safety_audit = false;
	bool found_files_under_review = false;
	bool found_extra_instructions = false;
	bool found_findings_file = false;
	bool found_headless_constraints = false;
	bool found_no_questions = false;

	for (const auto &msg : sub_convo) {
		if (msg.role == "system") {
			if (msg.content.find("C/C++ Buffer Safety Audit") != std::string::npos) {
				found_buffer_safety_audit = true;
			}
			if (msg.content.find("sec_src_to_review.cpp") != std::string::npos) {
				found_files_under_review = true;
			}
			if (msg.content.find("Focus on buffer overflows") != std::string::npos) {
				found_extra_instructions = true;
			}
			if (msg.content.find("findings.md") != std::string::npos) {
				found_findings_file = true;
			}
			if (msg.content.find("Headless Environment Constraints") != std::string::npos) {
				found_headless_constraints = true;
			}
			if (msg.content.find("MUST NOT ask") != std::string::npos) {
				found_no_questions = true;
			}
		}
	}

	assert(found_buffer_safety_audit);
	assert(found_files_under_review);
	assert(found_extra_instructions);
	assert(found_findings_file);
	assert(found_headless_constraints);
	assert(found_no_questions);

	// Cleanup
	fs_utils::set_override_project_dir("");
	unsetenv("TURBOSTAR_PROJECT_ROOT");
	project_manager::get_instance().initialize();
	std::filesystem::remove_all(temp_proj);
	std::cout << "security_review_with_agent tool verified successfully!" << std::endl;
}

void test_security_review_with_agent_empty_instructions()
{
	register_security_review_with_agent();
	std::cout << "Testing security_review_with_agent tool with empty instructions..." << std::endl;

	std::filesystem::path orig_path = std::filesystem::current_path();
	std::filesystem::path temp_proj = orig_path / "test_temp_sec_review_proj_empty";
	std::filesystem::remove_all(temp_proj);
	std::filesystem::create_directories(temp_proj);

	// Create a single test file we can review
	std::filesystem::path test_file_path = temp_proj / "sec_src_to_review.cpp";
	{
		std::ofstream ofs(test_file_path);
		ofs << "int main() {\n\treturn 0;\n}\n";
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

	auto model = std::make_shared<ai_model>("test-model", "Test Model", "http://localhost:1", "Test", 0.0, 0.0);
	ai_model_registry::get_instance().register_model(model);
	config_manager::get_instance().set_task_model_id("code_reviewer", "test-model");

	auto agent = ai_agent::create(3, "DeveloperAgentEmpty", model, &q, nullptr);
	agent->set_role(agent_role::developer);
	ctx.active_agent = agent.get();
	ctx.properties = agent->get_properties();

	std::string args_json = "{\"files\": [\"sec_src_to_review.cpp\"], \"instructions\": \"\", \"result_file\": \"findings.md\"}";
	auto prep = registry.prepare_tool("security_review_with_agent", args_json, ctx);
	assert(prep.tool != nullptr);

	std::string res_str = registry.execute_tool("security_review_with_agent", args_json, ctx);

	// Verify subagent was created
	auto subagents = agent->get_subagents();
	assert(!subagents.empty());
	auto subagent = subagents[0];
	assert(subagent->get_role() == agent_role::reviewer);
	assert(subagent->get_name() == "Security Reviewer");

	// Verify system prompt contents has the default instructions filled in
	auto sub_convo = subagent->get_conversation();
	assert(!sub_convo.empty());

	bool found_expected_instructions = false;
	for (const auto &msg : sub_convo) {
		if (msg.role == "system") {
			if (msg.content.find("Review sec_src_to_review.cpp for security and place the result in `findings.md`.") != std::string::npos) {
				found_expected_instructions = true;
			}
		}
	}
	assert(found_expected_instructions);

	// Cleanup
	fs_utils::set_override_project_dir("");
	unsetenv("TURBOSTAR_PROJECT_ROOT");
	project_manager::get_instance().initialize();
	std::filesystem::remove_all(temp_proj);
	std::cout << "security_review_with_agent tool empty instructions verified successfully!" << std::endl;
}

int main()
{
	test_watchdog::setup_watchdog(30);
	project_manager::get_instance().initialize();
	test_security_review_with_agent_execution();
	test_security_review_with_agent_empty_instructions();
	return 0;
}
