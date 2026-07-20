#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include <string>
#include <nlohmann/json.hpp>
#include "agentlib/ai_agent.h"
#include "agentlib/tool_registry.h"
#include "project_manager.h"
#include "event_queue.h"

using namespace agentlib;

class dummy_doc_provider : public document_provider {
public:
	std::vector<std::string> get_open_document_paths() const override { return {}; }
	std::unique_ptr<document_snapshot> get_open_document(const std::string&) const override { return nullptr; }
	bool apply_live_edits(const std::string&, const std::string&) override { return false; }
	void save_all_documents() override {}
	start_app_result start_coredump_gdb(const std::string& /*crash_id*/) override {
		return {42, 42};
	}
};

int main()
{
	test_watchdog::setup_watchdog(30);
	project_manager::get_instance().initialize();

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;
	event_queue q;
	dummy_doc_provider dummy;
	ctx.doc_provider = &dummy;

	ctx.fs_security.set_working_directory(project_manager::get_instance().get_project_root());
	ctx.fs_security.add_allowed_root(project_manager::get_instance().get_project_root(), access_type::read);
	ctx.fs_security.add_allowed_root(project_manager::get_instance().get_project_root(), access_type::write);

	auto model = std::make_shared<ai_model>("test-model", "Test Model", "http://localhost", "Test", 0.0, 0.0);
	auto agent = ai_agent::create(1, "TestAgent", model, &q, nullptr);
	ctx.active_agent = agent.get();

	std::cout << "Testing agent_debug_coredump..." << std::endl;
	{
		// 1. Success case with normal crash_id and check for GDB instructions
		{
			auto prep = registry.prepare_tool("agent_debug_coredump", "{\"crash_id\": \"12345\"}", ctx);
			assert(prep.tool != nullptr);
			assert(prep.error_message.empty());

			std::string res = registry.execute_tool("agent_debug_coredump", "{\"crash_id\": \"12345\"}", ctx);
			std::cout << "Debug coredump execution result: " << res << std::endl;
			nlohmann::json res_json = nlohmann::json::parse(res);
			assert(res_json.contains("gdb_run_id"));
			assert(res_json.contains("instructions"));
			assert(res_json["instructions"].get<std::string>().find("agent_terminate_run") != std::string::npos);
		}

		// 2. Reject empty crash_id
		{
			auto prep_empty = registry.prepare_tool("agent_debug_coredump", "{\"crash_id\": \"\"}", ctx);
			assert(prep_empty.tool == nullptr);
			assert(!prep_empty.error_message.empty());
		}

		// 3. Security case: reject command injection/special characters
		{
			auto prep_injection = registry.prepare_tool("agent_debug_coredump", "{\"crash_id\": \"12345; rm -rf /\"}", ctx);
			assert(prep_injection.tool == nullptr);
			assert(!prep_injection.error_message.empty());
		}

		std::cout << "agent_debug_coredump tool verified successfully!" << std::endl;
	}

	return 0;
}
