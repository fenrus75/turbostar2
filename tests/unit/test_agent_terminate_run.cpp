// Tested source file: src/tools/agent_terminate_run/agent_terminate_run_entry.cpp
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

// Create a dummy document provider to test non-null case
class dummy_doc_provider : public document_provider
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
	bool terminate_run(int run_id) override
	{
		return run_id > 0;
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

	std::cout << "Testing agent_terminate_run..." << std::endl;
	{
		// 1. Validation case: reject negative run_id
		{
			auto prep = registry.prepare_tool("agent_terminate_run", "{\"run_id\": -3}", ctx);
			assert(prep.tool == nullptr);
			assert(!prep.error_message.empty());
		}

		// 2. Runtime validation case: reject if doc_provider is null
		{
			ctx.doc_provider = nullptr;
			auto prep = registry.prepare_tool("agent_terminate_run", "{\"run_id\": 1}", ctx);
			assert(prep.tool == nullptr);
			assert(!prep.error_message.empty());
		}

		// 3. Runtime validation case: success if doc_provider is set
		{
			dummy_doc_provider dummy;
			ctx.doc_provider = &dummy;
			auto prep = registry.prepare_tool("agent_terminate_run", "{\"run_id\": 1}", ctx);
			assert(prep.tool != nullptr);
			std::string err;
			bool val = prep.tool->validate_runtime(ctx, err);
			assert(val);
			assert(err.empty());
		}

		// 4. Test profile notification inclusion
		{
			dummy_doc_provider dummy;
			ctx.doc_provider = &dummy;
			turbostar::perf_profile_report report;
			report.total_samples = 50;
			turbostar::perf_manager::get_instance().set_active_profile(report, "run_1");

			auto prep = registry.prepare_tool("agent_terminate_run", "{\"run_id\": 1}", ctx);
			assert(prep.tool != nullptr);
			std::string res = prep.tool->execute(ctx);
			assert(res.find("Performance profile data is available") != std::string::npos);

			turbostar::perf_manager::get_instance().clear_active_profile();
		}

		// 5. Test linked app & gdb termination reporting both IDs
		{
			dummy_doc_provider dummy;
			ctx.doc_provider = &dummy;
			ctx.run_registry->clear();
			ctx.run_registry->register_app_run(1010, "turbostar", 1011);
			ctx.run_registry->register_gdb_run(1011, 1010);

			// Terminate app: should mention both 1010 and 1011
			auto prep = registry.prepare_tool("agent_terminate_run", "{\"run_id\": 1010}", ctx);
			assert(prep.tool != nullptr);
			std::string res = prep.tool->execute(ctx);
			assert(res.find("1010") != std::string::npos);
			assert(res.find("1011") != std::string::npos);
			assert(res.find("associated debugger session") != std::string::npos);

			// 6. Double termination of linked gdb: should succeed benignly
			auto prep2 = registry.prepare_tool("agent_terminate_run", "{\"run_id\": 1011}", ctx);
			assert(prep2.tool != nullptr);
			std::string res2 = prep2.tool->execute(ctx);
			assert(res2.find("already terminated") != std::string::npos);
			assert(res2.find("1010") != std::string::npos);
		}

		// 7. Test default resolution (omitted run_id)
		{
			dummy_doc_provider dummy;
			ctx.doc_provider = &dummy;
			ctx.run_registry->clear();
			ctx.run_registry->register_app_run(2020, "app2");

			auto prep = registry.prepare_tool("agent_terminate_run", "{}", ctx);
			assert(prep.tool != nullptr);
			std::string res = prep.tool->execute(ctx);
			assert(res.find("2020") != std::string::npos);
		}

		std::cout << "agent_terminate_run tool verified successfully!" << std::endl;
	}

	return 0;
}
