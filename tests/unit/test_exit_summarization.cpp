#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "../../src/agentlib/ai_model.h"
#include "../../src/agentlib/document_provider.h"
#include "../../src/agentlib/interactions/interactions.h"
#include "../../src/agentlib/llm_client.h"
#include "../../src/agentlib/tool_registry.h"

#include "../../src/config_manager.h"
#include "../../src/event_logger.h"
#include "../../src/event_queue.h"
#include "../../src/fs_utils.h"
#include "../../src/project_manager.h"

#define private public
#include "../../src/agentlib/ai_agent.h"
#undef private

using namespace agentlib;

int main()
{
	event_logger::get_instance().enable_stdout_logging(true);
	project_manager::get_instance().initialize();

	std::string history_dir = fs_utils::get_project_history_dir("TestAgentExit");
	if (std::filesystem::exists(history_dir)) {
		std::filesystem::remove_all(history_dir);
	}

	event_queue q;
	auto model = std::make_shared<ai_model>("test-model", "Test Model", "http://localhost:1", "Test", 0.0, 0.0);
	ai_model_registry::get_instance().register_model(model);
	config_manager::get_instance().set_default_model_id("test-model");

	// 1. First test: with exiting = false
	project_manager::get_instance().set_exiting(false);
	{
		auto agent = ai_agent::create(1, "TestAgentExit", model, &q, nullptr);

		// Inject some context so it pages out
		agent->inject_context("user", "Hello");
		agent->inject_context("assistant", std::string(1100, 'x'));
		agent->inject_context("user", "More text");

		// Page out to trigger queuing a task
		agent->page_out_context(0, 2, "Test Segment", "test summary", {"test"});

		std::cout << "Initial summary queue size: " << agent->summary_queue_.size() << std::endl;
	}

	// 2. Second test: with exiting = true
	project_manager::get_instance().set_exiting(true);
	{
		auto agent = ai_agent::create(2, "TestAgentExit", model, &q, nullptr);

		// Inject some context so it pages out
		agent->inject_context("user", "Hello");
		agent->inject_context("assistant", std::string(1100, 'x'));
		agent->inject_context("user", "More text");

		// Page out when exiting is true
		agent->page_out_context(0, 2, "Test Segment", "test summary", {"test"});

		// The queue should definitely be empty because the is_exiting() check prevents pushing!
		std::cout << "Summary queue size when exiting: " << agent->summary_queue_.size() << std::endl;
		assert(agent->summary_queue_.empty());
	}

	std::cout << "test_exit_summarization passed successfully!" << std::endl;
	return 0;
}
