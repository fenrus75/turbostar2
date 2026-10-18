// Tested source file: src/editor.cpp
#include <cassert>
#include <iostream>
#include <memory>
#include <unordered_set>
#include <vector>

#include "agentlib/ai_agent.h"
#include "agentlib/ai_model.h"
#include "config_manager.h"
#include "editor.h"
#include "event_queue.h"
#include "project_manager.h"
#include "test_watchdog.h"

using namespace agentlib;

static event_queue g_queue;

static std::shared_ptr<ai_agent> make_test_agent(int id, const std::string &name)
{
	auto model = std::make_shared<ai_model>("test-model", "Test Model", "http://localhost:1", "Test", 0.0, 0.0);
	ai_model_registry::get_instance().register_model(model);
	config_manager::get_instance().set_default_model_id("test-model");
	return ai_agent::create(id, name, model, &g_queue, nullptr);
}

int main()
{
	test_watchdog::setup_watchdog(10);
	project_manager::get_instance().initialize();

	// Scenario 1: Dead parent with an alive subagent.
	// The parent agent has terminated (status == dead), but its subagent is still alive (status == idle).
	// collect_agents_recursive must traverse the subagents and collect the alive child.
	{
		auto parent = make_test_agent(9001, "DeadParent");
		auto child = parent->spawn_subagent("alive child task");
		assert(child != nullptr);

		// Setting parent to dead
		parent->set_status(agent_status::dead);
		assert(parent->get_status() == agent_status::dead);

		// Keep child alive
		child->set_status(agent_status::idle);
		assert(child->get_status() == agent_status::idle);

		std::vector<std::shared_ptr<ai_agent>> out_agents;
		std::unordered_set<int> seen_ids;
		editor::collect_agents_recursive(parent, out_agents, seen_ids);

		// The dead parent must NOT be in out_agents, but the alive child MUST be collected.
		assert(out_agents.size() == 1);
		assert(out_agents[0]->get_id() == child->get_id());

		child->close();
		parent->close();
	}

	// Scenario 2: Alive parent with alive child.
	// Both must be collected.
	{
		auto parent = make_test_agent(9002, "AliveParent");
		auto child = parent->spawn_subagent("alive child");
		assert(child != nullptr);

		parent->set_status(agent_status::idle);
		child->set_status(agent_status::thinking);

		std::vector<std::shared_ptr<ai_agent>> out_agents;
		std::unordered_set<int> seen_ids;
		editor::collect_agents_recursive(parent, out_agents, seen_ids);

		assert(out_agents.size() == 2);
		assert(out_agents[0]->get_id() == parent->get_id());
		assert(out_agents[1]->get_id() == child->get_id());

		child->close();
		parent->close();
	}

	// Scenario 3: Dead parent -> dead child -> alive grandchild.
	// Only the alive grandchild should be collected.
	{
		auto parent = make_test_agent(9003, "DeadParent");
		auto child = parent->spawn_subagent("dead child");
		auto grandchild = child->spawn_subagent("alive grandchild");

		parent->set_status(agent_status::dead);
		child->set_status(agent_status::dead);
		grandchild->set_status(agent_status::idle);

		std::vector<std::shared_ptr<ai_agent>> out_agents;
		std::unordered_set<int> seen_ids;
		editor::collect_agents_recursive(parent, out_agents, seen_ids);

		assert(out_agents.size() == 1);
		assert(out_agents[0]->get_id() == grandchild->get_id());

		grandchild->close();
		child->close();
		parent->close();
	}

	// Scenario 4: Null agent handling.
	{
		std::vector<std::shared_ptr<ai_agent>> out_agents;
		std::unordered_set<int> seen_ids;
		editor::collect_agents_recursive(nullptr, out_agents, seen_ids);
		assert(out_agents.empty());
	}

	std::cout << "All collect_agents_recursive tests passed!\n";
	return 0;
}
