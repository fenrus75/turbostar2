#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/project_manager.h"
#include "../../src/event_queue.h"

using namespace agentlib;

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
	ctx.queue = &q;

	std::cout << "Testing ask_user..." << std::endl;
	{
		// 1. Success case: ask user and receive response via event queue
		{
			std::thread worker([&q]() {
				while (true) {
					auto ev = q.pop();
					if (ev) {
						if (ev->type == event_type::prompt_user) {
							assert(ev->payload == "Ready to proceed?");
							assert(ev->prompt_options.size() == 2);
							assert(ev->prompt_options[0] == "Yes");
							ev->prompt_promise->set_value("Yes");
							break;
						}
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(5));
				}
			});

			std::string res = registry.execute_tool("ask_user",
				"{\"questions\": [{\"question\": \"Ready to proceed?\", \"options\": [\"Yes\", \"No\"]}]}", ctx);
			worker.join();

			std::cout << "Response received: " << res << std::endl;
			assert(res == "Yes");
		}

		// 2. Stage 1 validation case: reject empty questions array (based on schema)
		{
			auto prep = registry.prepare_tool("ask_user", "{\"questions\": []}", ctx);
			assert(prep.tool == nullptr);
			assert(!prep.error_message.empty());
		}

		// 3. Stage 1 validation case: reject question missing 'question' string
		{
			auto prep = registry.prepare_tool("ask_user", "{\"questions\": [{\"options\": [\"Yes\"]}]}", ctx);
			assert(prep.tool == nullptr);
			assert(!prep.error_message.empty());
		}

		// 4. Stage 1 validation case: reject unexpected properties (based on review recommendations)
		{
			auto prep = registry.prepare_tool("ask_user",
				"{\"questions\": [{\"question\": \"Ready?\"}], \"unexpected_property\": true}", ctx);
			assert(prep.tool == nullptr); // This will fail initially as expected
			assert(!prep.error_message.empty());
		}

		// 5. Stage 2 validation case: reject if event queue is null (based on review recommendations)
		{
			ctx.queue = nullptr;
			auto prep = registry.prepare_tool("ask_user", "{\"questions\": [{\"question\": \"Ready?\"}]}", ctx);
			assert(prep.tool == nullptr); // This will fail initially as expected
			assert(!prep.error_message.empty());
		}

		std::cout << "ask_user tool verified successfully!" << std::endl;
	}

	return 0;
}
