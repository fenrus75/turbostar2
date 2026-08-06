#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/command_registry.h"
#include "../../src/project_manager.h"
#include "../../src/event_queue.h"
#include "../../src/fs_utils.h"

using namespace agentlib;

int main()
{
	test_watchdog::setup_watchdog(30);
	project_manager::get_instance().initialize();

	auto model = std::make_shared<ai_model>("test-model", "Test Model", "http://localhost", "Test", 0.0, 0.0);
	static event_queue q;
	auto agent = ai_agent::create(1, "TestClearAgent", model, &q, nullptr);
	message sys_msg;
	sys_msg.role = "system";
	sys_msg.content = "System baseline prompt";
	std::vector<message> init_msgs = {sys_msg};
	agent->set_conversation(init_msgs);

	std::cout << "Testing /clear command..." << std::endl;

	// Populate agent state
	agent->add_active_skill("demo_skill");
	assert(!agent->get_active_skills().empty());

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
	assert(agent->get_subagents().empty());
	assert(agent->get_model() == model);

	const auto &interactions = agent->get_interactions();
	assert(!interactions.empty());
	assert(interactions.back()->get_raw_text().find("Agent context cleared") != std::string::npos);

	// Verify system prompt is preserved in conversation messages
	auto msgs = agent->get_conversation();
	assert(!msgs.empty());
	assert(msgs[0].role == "system");
	assert(!msgs[0].content.empty());

	// Test restart persistence after /clear
	// 1. Create episode archive
	agent->inject_context("user", "Heavy work turn 1");
	agent->inject_context("assistant", "Working on task");
	agent->page_out_context(1, agent->get_conversation().size(), "Episode 1", "Heavy work done", {"test"});

	std::string history_dir = fs_utils::get_project_history_dir("TestClearAgent");
	assert(std::filesystem::exists(history_dir + "/episode_1.json"));

	// 2. Clear history
	cmd->execute(ctx);
	assert(!std::filesystem::exists(history_dir + "/episode_1.json"));

	// 3. Simulate application restart
	auto agent2 = ai_agent::create(1, "TestClearAgent", model, &q, nullptr);
	bool loaded = agent2->load_active_state();
	assert(loaded);

	auto restarted_msgs = agent2->get_conversation();
	for (const auto &m : restarted_msgs) {
		assert(m.content.find("Heavy work turn 1") == std::string::npos);
		assert(m.content.find("Episode 1") == std::string::npos);
	}

	std::cout << "/clear command verified successfully!" << std::endl;
	return 0;
}
