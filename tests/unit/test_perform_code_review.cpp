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

void test_perform_code_review_execution()
{
	std::cout << "Testing perform_code_review tool..." << std::endl;

	std::filesystem::path orig_path = std::filesystem::current_path();
	std::filesystem::path temp_proj = orig_path / "test_temp_review_proj";
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
	auto agent = ai_agent::create(1, "DeveloperAgent", model, &q, nullptr);
	agent->set_role(agent_role::developer);
	ctx.active_agent = agent.get();

	// Test 1: Role gating (reviewer role should be blocked from calling this tool to avoid recursion)
	auto reviewer_temp = ai_agent::create(2, "TempReviewer", model, &q, nullptr);
	reviewer_temp->set_role(agent_role::reviewer);
	tool_context reviewer_ctx = ctx;
	reviewer_ctx.active_agent = reviewer_temp.get();

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
	bool found_file_content = false;
	bool found_p_42 = false;

	for (const auto &msg : sub_convo) {
		if (msg.content.find("Old Null Pointer") != std::string::npos) {
			found_old_null_pointer = true;
		}
		if (msg.content.find("File Content of src_to_review.cpp:") != std::string::npos) {
			found_file_content = true;
		}
		if (msg.content.find("*p = 42;") != std::string::npos) {
			found_p_42 = true;
		}
	}

	assert(found_old_null_pointer);
	assert(found_file_content);
	assert(found_p_42);

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

	// Cleanup
	fs_utils::set_override_project_dir("");
	unsetenv("TURBOSTAR_PROJECT_ROOT");
	project_manager::get_instance().initialize();
	std::filesystem::remove_all(temp_proj);
	std::cout << "perform_code_review tool and sandbox constraints verified successfully!" << std::endl;
}

int main()
{
	project_manager::get_instance().initialize();
	test_perform_code_review_execution();
	return 0;
}
