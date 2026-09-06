// Tested source file: src/tools/agent_get_run_screenshot/agent_get_run_screenshot_entry.cpp
#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>
#include "agentlib/ai_agent.h"
#include "agentlib/tool_registry.h"
#include "project_manager.h"
#include "event_queue.h"
#include "tools/agent_get_run_screenshot/agent_get_run_screenshot.h"

using namespace agentlib;

class mock_document_provider : public document_provider {
public:
	std::vector<std::string> get_open_document_paths() const override { return {}; }
	std::unique_ptr<document_snapshot> get_open_document(std::string_view) const override { return nullptr; }
	bool apply_live_edits(std::string_view, std::string_view) override { return false; }
	void save_all_documents() override {}

	run_screenshot_data get_run_screenshot(int run_id) override {
		if (run_id == 42) {
			return {{"line1", "line2"}, 10, 5, true, true, ""};
		}
		if (run_id == 43) {
			return {{"line1", "line2"}, 0, 0, false, false, "CRASH DETECTED"};
		}
		if (run_id == 44) {
			return {{"hello world    ", "  indented    ", "              ", "              "}, 11, 0, true, true, ""};
		}
		return {};
	}

	int64_t get_run_last_modified_age(int) override {
		return 500; // Simulated age of 500ms (already settled)
	}
};

int main()
{
	test_watchdog::setup_watchdog(30);
	project_manager::get_instance().initialize();

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;
	event_queue q;

	ctx.fs_security.set_working_directory(project_manager::get_instance().get_project_root());
	ctx.fs_security.add_allowed_root(project_manager::get_instance().get_project_root(), access_type::read);
	ctx.fs_security.add_allowed_root(project_manager::get_instance().get_project_root(), access_type::write);

	auto model = std::make_shared<ai_model>("test-model", "Test Model", "http://localhost", "Test", 0.0, 0.0);
	auto agent = ai_agent::create(1, "TestAgent", model, &q, nullptr);
	ctx.active_agent = agent.get();

	mock_document_provider provider;
	ctx.doc_provider = &provider;

	std::cout << "Testing agent_get_run_terminaldump & agent_get_run_screenshot..." << std::endl;
	{
		// 1. Success case with new Markdown format
		std::string result = registry.execute_tool("agent_get_run_terminaldump", "{\"run_id\": 42}", ctx);
		std::cout << "Result (agent_get_run_terminaldump):\n" << result << std::endl;
		assert(result.find("<agent_get_run_terminaldump_result>") != std::string::npos);
		assert(result.find("[Terminal Screen (cursor at row 5, col 10; process running)]:") != std::string::npos);
		assert(result.find("```\nline1\nline2\n```") != std::string::npos);

		// 1b. Backward compatibility alias: agent_get_run_screenshot
		std::string result_alias = registry.execute_tool("agent_get_run_screenshot", "{\"run_id\": 42}", ctx);
		assert(result_alias.find("<agent_get_run_screenshot_result>") != std::string::npos);
		assert(result_alias.find("[Terminal Screen (cursor at row 5, col 10; process running)]:") != std::string::npos);
		assert(result_alias.find("```\nline1\nline2\n```") != std::string::npos);

		// 1c. Aliases: agent_run_get_terminaldump and agent_run_get_screenshot
		std::string result_alias2 = registry.execute_tool("agent_run_get_terminaldump", "{\"run_id\": 42}", ctx);
		assert(result_alias2.find("<agent_run_get_terminaldump_result>") != std::string::npos);
		assert(result_alias2.find("```\nline1\nline2\n```") != std::string::npos);

		std::string result_alias3 = registry.execute_tool("agent_run_get_screenshot", "{\"run_id\": 42}", ctx);
		assert(result_alias3.find("<agent_run_get_screenshot_result>") != std::string::npos);
		assert(result_alias3.find("```\nline1\nline2\n```") != std::string::npos);

		// 1d. Trimming trailing spaces and trailing empty lines (run_id 44)
		std::string result_trim = registry.execute_tool("agent_get_run_terminaldump", "{\"run_id\": 44}", ctx);
		std::cout << "Result (run_id 44 trimming):\n" << result_trim << std::endl;
		assert(result_trim.find("[Terminal Screen (cursor at row 0, col 11; process running)]:") != std::string::npos);
		assert(result_trim.find("```\nhello world\n  indented\n```") != std::string::npos);
		assert(result_trim.find("              ") == std::string::npos);

		// 2. Crash notification and ended process case
		{
			std::string res_crash = registry.execute_tool("agent_get_run_terminaldump", "{\"run_id\": 43}", ctx);
			assert(res_crash.find("CRASH DETECTED") != std::string::npos);
			assert(res_crash.find("process ended") != std::string::npos);
			assert(res_crash.find("cursor hidden") != std::string::npos);
		}

		// 3. Failure: Run ID not found
		result = registry.execute_tool("agent_get_run_terminaldump", "{\"run_id\": 999}", ctx);
		assert(result.find("Error") != std::string::npos);

		// 4. Reject negative ID
		{
			auto prep = registry.prepare_tool("agent_get_run_terminaldump", "{\"run_id\": -10}", ctx);
			assert(prep.tool == nullptr);
			assert(prep.error_message.find("Invalid") != std::string::npos);
		}

		// 5. Reject missing run_id
		{
			auto prep = registry.prepare_tool("agent_get_run_terminaldump", "{}", ctx);
			assert(prep.tool == nullptr);
			assert(prep.error_message.find("required") != std::string::npos);
		}

		// 6. Reject if doc_provider is missing
		{
			ctx.doc_provider = nullptr;
			auto prep = registry.prepare_tool("agent_get_run_terminaldump", "{\"run_id\": 42}", ctx);
			assert(prep.tool == nullptr);
			assert(prep.error_message.find("provider") != std::string::npos);

			// Directly test validate_runtime
			tools::agent_get_run_screenshot_tool direct_tool({42});
			std::string direct_err;
			assert(direct_tool.validate_runtime(ctx, direct_err) == false);
			assert(direct_err.find("provider") != std::string::npos);

			ctx.doc_provider = &provider;
		}

		// 7. Success case with settle
		{
			std::string result_settle = registry.execute_tool("agent_get_run_terminaldump", "{\"run_id\": 42, \"settle\": true}", ctx);
			assert(result_settle.find("line1") != std::string::npos);
		}

		std::cout << "agent_get_run_terminaldump & aliases verified successfully!" << std::endl;
	}

	return 0;
}
