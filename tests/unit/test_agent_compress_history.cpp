#include <cassert>
#include <iostream>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/project_manager.h"
#include "../../src/event_queue.h"
#include "tools/agent_compress_history/agent_compress_history.h"

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

	// Inject some conversational context to compress
	agent->inject_context("user", "Hello world");
	agent->inject_context("assistant", "Hi there!");
	agent->inject_context("user", "Let's do some work.");

	std::cout << "Testing agent_compress_history..." << std::endl;
	{
		// 1. Successful execution
		std::string compress_res = registry.execute_tool("agent_compress_history",
			"{\"title\": \"Milestone 1\", \"summary\": \"Completed first step.\"}", ctx);
		std::cout << "Result: " << compress_res << std::endl;
		assert(compress_res.find("successfully") != std::string::npos);

		// 2. Rejection of empty title (validation fails at runtime)
		{
			tools::agent_compress_history_tool tool1({"", "summary", {}, "", false});
			std::string err;
			assert(tool1.validate_runtime(ctx, err) == false);
			assert(err.find("title") != std::string::npos);
		}

		// 3. Rejection of empty summary
		{
			tools::agent_compress_history_tool tool2({"title", "", {}, "", false});
			std::string err;
			assert(tool2.validate_runtime(ctx, err) == false);
			assert(err.find("summary") != std::string::npos);
		}

		// 4. Rejection of overly long title (> 200 characters)
		{
			std::string long_title(201, 'a');
			auto prep = registry.prepare_tool("agent_compress_history",
				"{\"title\": \"" + long_title + "\", \"summary\": \"summary\"}", ctx);
			assert(prep.tool == nullptr);
			assert(prep.error_message.find("Title exceeds") != std::string::npos);
		}

		// 5. Rejection of overly long summary (> 2000 characters)
		{
			std::string long_summary(2001, 'b');
			auto prep = registry.prepare_tool("agent_compress_history",
				"{\"title\": \"title\", \"summary\": \"" + long_summary + "\"}", ctx);
			assert(prep.tool == nullptr);
			assert(prep.error_message.find("Summary exceeds") != std::string::npos);
		}

		// 6. Rejection of unsafe control characters in title
		{
			auto prep = registry.prepare_tool("agent_compress_history",
				"{\"title\": \"unsafe\\u001btitle\", \"summary\": \"summary\"}", ctx);
			assert(prep.tool == nullptr);
			assert(prep.error_message.find("unsafe control characters") != std::string::npos);
		}

		// 7. Rejection if agent is read-only
		auto original_ro = agent->is_read_only();
		agent->set_read_only(true);
		auto prep = registry.prepare_tool("agent_compress_history",
			"{\"title\": \"Valid title\", \"summary\": \"Valid summary\"}", ctx);
		assert(prep.tool == nullptr);
		assert(prep.error_message.find("read-only") != std::string::npos);

		// Directly test validate_runtime on the tool under read-only state
		{
			tools::agent_compress_history_tool direct_tool({"Valid title", "Valid summary", {}, "", false});
			std::string direct_err;
			assert(direct_tool.validate_runtime(ctx, direct_err) == false);
			assert(direct_err.find("read-only") != std::string::npos);
		}

		agent->set_read_only(original_ro);
		std::cout << "agent_compress_history tool verified successfully!" << std::endl;
	}

	return 0;
}
