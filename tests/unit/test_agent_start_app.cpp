#include <cassert>
#include <iostream>
#include <string>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/project_manager.h"
#include "../../src/event_queue.h"

using namespace agentlib;

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
	dummy_doc_provider dummy;
	ctx.doc_provider = &dummy;

	ctx.fs_security.set_working_directory(project_manager::get_instance().get_project_root());
	ctx.fs_security.add_allowed_root(project_manager::get_instance().get_project_root(), access_type::read);
	ctx.fs_security.add_allowed_root(project_manager::get_instance().get_project_root(), access_type::write);

	auto model = std::make_shared<ai_model>("test-model", "Test Model", "http://localhost", "Test", 0.0, 0.0);
	auto agent = ai_agent::create(1, "TestAgent", model, &q, nullptr);
	ctx.active_agent = agent.get();

	std::cout << "Testing agent_start_app..." << std::endl;
	{
		// 1. Success case with normal arguments
		// Since we only prepare/validate the tool arguments, we can check if validation passes
		auto prep = registry.prepare_tool("agent_start_app", "{\"args\": \"--version\", \"debugger\": false}", ctx);
		assert(prep.tool != nullptr);
		assert(prep.error_message.empty());

		// 2. Security case: reject command injection / metacharacters in arguments (based on review recommendations)
		{
			auto prep_injection = registry.prepare_tool("agent_start_app", "{\"args\": \"; rm -rf /\", \"debugger\": false}", ctx);
			assert(prep_injection.tool == nullptr); // This will fail initially
			assert(!prep_injection.error_message.empty());
		}

		{
			auto prep_injection = registry.prepare_tool("agent_start_app", "{\"args\": \"args && bad_command\", \"debugger\": false}", ctx);
			assert(prep_injection.tool == nullptr); // This will fail initially
			assert(!prep_injection.error_message.empty());
		}

		std::cout << "agent_start_app tool verified successfully!" << std::endl;
	}

	return 0;
}
