#include <cassert>
#include <iostream>
#include <string>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/project_manager.h"
#include "../../src/event_queue.h"

using namespace agentlib;

// Create a dummy document provider to test non-null case
class dummy_doc_provider : public document_provider {
public:
	std::vector<std::string> get_open_document_paths() const override { return {}; }
	std::unique_ptr<document_snapshot> get_open_document(const std::string&) const override { return nullptr; }
	bool apply_live_edits(const std::string&, const std::string&) override { return false; }
	void save_all_documents() override {}
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

		std::cout << "agent_terminate_run tool verified successfully!" << std::endl;
	}

	return 0;
}
