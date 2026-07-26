#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include <string>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/project_manager.h"
#include "../../src/event_queue.h"
#include "../../src/perf_manager.h"

using namespace agentlib;

class dummy_doc_provider_wait : public document_provider {
public:
	std::vector<std::string> get_open_document_paths() const override { return {}; }
	std::unique_ptr<document_snapshot> get_open_document(const std::string&) const override { return nullptr; }
	bool apply_live_edits(const std::string&, const std::string&) override { return false; }
	void save_all_documents() override {}

	wait_for_app_result wait_for_app(int run_id, const std::string& type, int /*timeout_sec*/) override {
		if (run_id == 42) {
			if (type == "settled") {
				return {"settled", 1500, true, ""};
			} else if (type == "ended") {
				return {"ended", 50, false, "CRASHED WITH SIGSEGV"};
			}
		}
		return {"not_found", 0, false, ""};
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
			std::string res = registry.execute_tool("agent_wait_for_app", R"({"run_id": 42, "type": "settled", "timeout_sec": 5})", ctx);
			assert(res.find("settled") != std::string::npos);
			assert(res.find("1500") != std::string::npos);
		}

		// 5. Execution case: ended with crash notification
		{
			dummy_doc_provider_wait dummy;
			ctx.doc_provider = &dummy;
			std::string res = registry.execute_tool("agent_wait_for_app", R"({"run_id": 42, "type": "ended", "timeout_sec": 5})", ctx);
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

		std::cout << "agent_wait_for_app tool verified successfully!" << std::endl;
	}

	return 0;
}
