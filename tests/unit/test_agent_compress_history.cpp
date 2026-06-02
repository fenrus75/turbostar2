#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>
#include <fstream>
#include <filesystem>
#include "../../src/event_logger.h"
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/project_manager.h"
#include "../../src/config_manager.h"
#include "../../src/fs_utils.h"
#include "../../src/event_queue.h"
#include "tools/agent_compress_history/agent_compress_history.h"

using namespace agentlib;

int main()
{
	event_logger::get_instance().enable_stdout_logging(true);
	project_manager::get_instance().initialize();

	// Clear history directory to ensure clean test state
	std::string history_dir = fs_utils::get_project_history_dir("TestAgent");
	if (std::filesystem::exists(history_dir)) {
		std::filesystem::remove_all(history_dir);
	}

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;
	event_queue q;

	ctx.fs_security.set_working_directory(project_manager::get_instance().get_project_root());
	ctx.fs_security.add_allowed_root(project_manager::get_instance().get_project_root(), access_type::read);
	ctx.fs_security.add_allowed_root(project_manager::get_instance().get_project_root(), access_type::write);

	auto model = std::make_shared<ai_model>("test-model", "Test Model", "http://localhost:1", "Test", 0.0, 0.0);
	ai_model_registry::get_instance().register_model(model);
	config_manager::get_instance().set_default_model_id("test-model");
	auto agent = ai_agent::create(1, "TestAgent", model, &q, nullptr);
	ctx.active_agent = agent.get();

	// Inject some conversational context to compress
	agent->inject_context("user", "Hello world");
	agent->inject_context("assistant", std::string(1100, 'x'));
	agent->inject_context("user", "Let's do some work.");
	agent->inject_context("assistant", "Sure, what do you want to do?");
	agent->inject_context("user", "Write a python script.");
	agent->inject_context("assistant", "Okay, here is a python script.");
	agent->inject_context("user", "Now run it.");
	agent->inject_context("assistant", "It ran successfully.");

	std::cout << "Testing agent_compress_history..." << std::endl;
	std::cout << "Conversation size: " << agent->get_conversation().size() << std::endl;
	{
		// 1. Successful execution
		std::string compress_res = registry.execute_tool("agent_compress_history",
			"{\"title\": \"Milestone 1\", \"summary\": \"Completed first step.\", \"include_all_prior\": true}", ctx);
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

		// Wait for background summary worker thread to process and fail (connecting to localhost)
		std::cout << "Waiting for background summarization to fail..." << std::endl;
		std::this_thread::sleep_for(std::chrono::milliseconds(2000));

		// Check the metadata files to ensure reactivation_hint does not store the error message
		std::string history_dir = fs_utils::get_project_history_dir(agent->get_name());
		bool found_metadata = false;
		for (const auto& entry : std::filesystem::directory_iterator(history_dir)) {
			std::string filename = entry.path().filename().string();
			if (filename.ends_with("_metadata.json")) {
				found_metadata = true;
				std::ifstream f(entry.path());
				nlohmann::json root;
				f >> root;
				std::string hint = root.value("reactivation_hint", "");
				std::cout << "Metadata file: " << filename << ", reactivation_hint: '" << hint << "'" << std::endl;
				// The reactivation hint must not contain the connection error message
				assert(hint.find("Error connecting to LLM server") == std::string::npos);
			}
		}
		assert(found_metadata);

		std::cout << "agent_compress_history tool verified successfully!" << std::endl;
	}

	return 0;
}
