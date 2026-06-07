#include <cassert>
#include <iostream>
#include <string>
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

	std::cout << "Testing agent_mark_episode..." << std::endl;
	{
		// 1. Success case with normal parameters
		std::string result = registry.execute_tool("agent_mark_episode",
			"{\"title\": \"Episode 1\", \"summary\": \"Completed task A\", \"tags\": [\"milestone\"]}", ctx);
		std::cout << "Result: " << result << std::endl;
		assert(result.find("Episode marked") != std::string::npos || result.find("archived") != std::string::npos || result.find("Episode manually recorded") != std::string::npos);

		// Test reminder message integration in mark_episode
		agent->add_todo("Next task");
		std::string result_with_reminder = registry.execute_tool("agent_mark_episode",
			"{\"title\": \"Episode 2\", \"summary\": \"Completed task B\", \"tags\": [\"milestone\"]}", ctx);
		std::cout << "Result with reminder: " << result_with_reminder << std::endl;
		assert(result_with_reminder.find("1 todo item remaining. Next todo item: 'Next task'") != std::string::npos);
		
		// Clean up todo
		std::string err;
		agent->delete_todo("*", err);

		// 2. Security case: reject unexpected arguments (based on review recommendations)
		{
			auto prep = registry.prepare_tool("agent_mark_episode",
				"{\"title\": \"Episode 1\", \"summary\": \"Completed task A\", \"tags\": [\"milestone\"], \"extra\": 123}", ctx);
			assert(prep.tool == nullptr);
			assert(!prep.error_message.empty());
		}

		// 3. Security case: reject overly long title (> 500 characters)
		{
			std::string long_title(501, 'a');
			auto prep = registry.prepare_tool("agent_mark_episode",
				"{\"title\": \"" + long_title + "\", \"summary\": \"Summary\", \"tags\": []}", ctx);
			assert(prep.tool == nullptr);
			assert(!prep.error_message.empty());
		}

		// 4. Security case: reject overly long summary (> 500 characters)
		{
			std::string long_summary(501, 'b');
			auto prep = registry.prepare_tool("agent_mark_episode",
				"{\"title\": \"Title\", \"summary\": \"" + long_summary + "\", \"tags\": []}", ctx);
			assert(prep.tool == nullptr);
			assert(!prep.error_message.empty());
		}

		std::cout << "agent_mark_episode tool verified successfully!" << std::endl;
	}

	return 0;
}
