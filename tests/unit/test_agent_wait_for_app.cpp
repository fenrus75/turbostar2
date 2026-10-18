// Tested source file: src/tools/agent_wait_for_app/agent_wait_for_app_entry.cpp
#include <cassert>
#include <iostream>
#include <string>
#include "agentlib/agent_run_registry.h"
#include "agentlib/ai_agent.h"
#include "agentlib/tool_registry.h"
#include "event_queue.h"
#include "perf_manager.h"
#include "project_manager.h"
#include "test_watchdog.h"

using namespace agentlib;

class dummy_doc_provider_wait : public document_provider
{
      public:
	std::vector<std::string> get_open_document_paths() const override
	{
		return {};
	}
	std::unique_ptr<document_snapshot> get_open_document(std::string_view) const override
	{
		return nullptr;
	}
	bool apply_live_edits(std::string_view, std::string_view) override
	{
		return false;
	}
	void save_all_documents() override
	{
	}

	wait_for_app_result wait_for_app(int run_id, std::string_view type, int /*timeout_sec*/) override
	{
		if (run_id == 42) {
			if (type == "settled") {
				return {.status = "settled", .age_ms = 1500, .is_alive = true, .crash_notification = ""};
			} else if (type == "ended") {
				return {.status = "ended", .age_ms = 50, .is_alive = false, .crash_notification = "CRASHED WITH SIGSEGV"};
			}
		}
		return {.status = "not_found", .age_ms = 0, .is_alive = false, .crash_notification = ""};
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

	std::cout << "Testing agent_wait_for_app..." << std::endl;
	{
		// 1. Validation case: reject invalid run_id
		{
			auto prep = registry.prepare_tool("agent_wait_for_app", R"({"run_id": -1})", ctx);
			assert(prep.tool == nullptr);
			assert(!prep.error_message.empty());
		}

		// 2. Validation case: reject invalid type
		{
			auto prep = registry.prepare_tool("agent_wait_for_app", R"({"run_id": 1, "type": "invalid"})", ctx);
			assert(prep.tool == nullptr);
			assert(!prep.error_message.empty());
		}

		// 3. Runtime validation case: reject if doc_provider is null
		{
			ctx.doc_provider = nullptr;
			auto prep = registry.prepare_tool("agent_wait_for_app", R"({"run_id": 1})", ctx);
			assert(prep.tool == nullptr);
			assert(!prep.error_message.empty());
		}

		// 4. Execution case: settled
		{
			dummy_doc_provider_wait dummy;
			ctx.doc_provider = &dummy;
			std::string res =
			    registry.execute_tool("agent_wait_for_app", R"({"run_id": 42, "type": "settled", "timeout_sec": 5})", ctx);
			assert(res.find("settled") != std::string::npos);
			assert(res.find("1500") != std::string::npos);
		}

		// 5. Execution case: ended with crash notification
		{
			dummy_doc_provider_wait dummy;
			ctx.doc_provider = &dummy;
			std::string res =
			    registry.execute_tool("agent_wait_for_app", R"({"run_id": 42, "type": "ended", "timeout_sec": 5})", ctx);
			assert(res.find("ended") != std::string::npos);
			assert(res.find("CRASHED WITH SIGSEGV") != std::string::npos);
		}

		// 6. Execution case: not found
		{
			dummy_doc_provider_wait dummy;
			ctx.doc_provider = &dummy;
			std::string res = registry.execute_tool("agent_wait_for_app", R"({"run_id": 999})", ctx);
			assert(res.find("not_found") != std::string::npos);
		}

		// 7. Execution case: profile notification when active profile exists
		{
			dummy_doc_provider_wait dummy;
			ctx.doc_provider = &dummy;
			turbostar::perf_profile_report report;
			report.total_samples = 100;
			turbostar::perf_manager::get_instance().set_active_profile(report, "run_42");

			std::string res = registry.execute_tool("agent_wait_for_app", R"({"run_id": 42})", ctx);
			assert(res.find("profile_notification") != std::string::npos);
			assert(res.find("Performance profile data is available") != std::string::npos);

			turbostar::perf_manager::get_instance().clear_active_profile();
		}

		// 8. Execution case: alias and omitted run_id
		{
			dummy_doc_provider_wait dummy;
			ctx.doc_provider = &dummy;
			ctx.run_registry->clear();
			ctx.run_registry->register_app_run(42, "test_binary");

			// Test alias "app"
			std::string res_alias =
			    registry.execute_tool("agent_wait_for_app", R"({"run_id": "app", "type": "settled", "timeout_sec": 5})", ctx);
			assert(res_alias.find("settled") != std::string::npos);

			// Test omitted run_id
			std::string res_omit = registry.execute_tool("agent_wait_for_app", R"({"type": "settled", "timeout_sec": 5})", ctx);
			assert(res_omit.find("settled") != std::string::npos);
		}

		std::cout << "agent_wait_for_app tool verified successfully!" << std::endl;
	}

	return 0;
}
