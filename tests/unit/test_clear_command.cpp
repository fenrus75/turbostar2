#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/command_registry.h"
#include "../../src/project_manager.h"
#include "../../src/event_queue.h"

using namespace agentlib;

int main()
{
	test_watchdog::setup_watchdog(30);
	project_manager::get_instance().initialize();

	auto model = std::make_shared<ai_model>("test-model", "Test Model", "http://localhost", "Test", 0.0, 0.0);
	static event_queue q;
	auto agent = ai_agent::create(1, "TestClearAgent", model, &q, nullptr);

	std::cout << "Testing /clear command..." << std::endl;

	// Populate agent state
	agent->add_todo("Test TODO item");
	assert(!agent->get_todos().empty());

	agent->add_interaction(std::make_shared<interaction_user_message>("Hello agent"));
	assert(!agent->get_interactions().empty());

	// Execute /clear slash command
	auto cmd = command_registry::get_instance().get_command("clear");
	assert(cmd != nullptr);

	agent_command::context ctx;
	ctx.agent = agent.get();
	ctx.window_id = 1;
	ctx.global_queue = &q;

	cmd->execute(ctx);

	// Verify state after clear
	assert(agent->get_todos().empty());
	assert(agent->get_subagents().empty());
	assert(agent->get_model() == model);

	const auto &interactions = agent->get_interactions();
	assert(!interactions.empty());
	assert(interactions.back()->get_raw_text().find("Agent context cleared") != std::string::npos);

	std::cout << "/clear command verified successfully!" << std::endl;
	return 0;
}
