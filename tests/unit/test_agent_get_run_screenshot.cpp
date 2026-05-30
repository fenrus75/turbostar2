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
	std::unique_ptr<document_snapshot> get_open_document(const std::string&) const override { return nullptr; }
	bool apply_live_edits(const std::string&, const std::string&) override { return false; }
	void save_all_documents() override {}

	run_screenshot_data get_run_screenshot(int run_id) override {
		if (run_id == 42) {
			return {{"line1", "line2"}, 10, 5, true};
		}
		return {};
	}
};

int main()
{
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

	std::cout << "Testing agent_get_run_screenshot..." << std::endl;
	{
		// 1. Success case
		std::string result = registry.execute_tool("agent_get_run_screenshot", "{\"run_id\": 42}", ctx);
		std::cout << "Result:\n" << result << std::endl;
		assert(result.find("line1") != std::string::npos);
		assert(result.find("cursor_x") != std::string::npos);
		assert(result.find("10") != std::string::npos);

		// 2. Failure: Run ID not found
		result = registry.execute_tool("agent_get_run_screenshot", "{\"run_id\": 999}", ctx);
		assert(result.find("Error") != std::string::npos);

		// 3. Reject negative ID
		{
			auto prep = registry.prepare_tool("agent_get_run_screenshot", "{\"run_id\": -10}", ctx);
			assert(prep.tool == nullptr);
			assert(prep.error_message.find("Invalid") != std::string::npos);
		}

		// 4. Reject missing run_id
		{
			auto prep = registry.prepare_tool("agent_get_run_screenshot", "{}", ctx);
			assert(prep.tool == nullptr);
			assert(prep.error_message.find("required argument") != std::string::npos);
		}

		// 5. Reject if doc_provider is missing
		{
			ctx.doc_provider = nullptr;
			auto prep = registry.prepare_tool("agent_get_run_screenshot", "{\"run_id\": 42}", ctx);
			assert(prep.tool == nullptr);
			assert(prep.error_message.find("provider") != std::string::npos);

			// Directly test validate_runtime
			tools::agent_get_run_screenshot_tool direct_tool({42});
			std::string direct_err;
			assert(direct_tool.validate_runtime(ctx, direct_err) == false);
			assert(direct_err.find("provider") != std::string::npos);

			ctx.doc_provider = &provider;
		}

		std::cout << "agent_get_run_screenshot tool verified successfully!" << std::endl;
	}

	return 0;
}
