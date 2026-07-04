/*
 * Matt Pocock Skills Plugin
 *
 * This plugin registers productivity skills from Matt Pocock's skills repository
 * (https://github.com/mattpocock/skills).
 *
 * Credit:
 * Matt Pocock (c) 2024.
 * Licensed under the MIT License.
 *
 * Included skills:
 * - grill-me: Relentless interactive interview to resolve design decisions.
 */

#include "agentlib/agent_command.h"
#include "agentlib/command_registry.h"
#include "agentlib/skill_manager.h"
#include "agentlib/ai_agent.h"
#include <memory>

class grill_me_command : public agent_command
{
public:
	std::string get_name() const override { return "grill-me"; }
	std::string get_description() const override { return "Grills you relentlessly about your plan or design until reaching shared understanding"; }
	void execute(const context &ctx) override
	{
		if (!ctx.agent) {
			return;
		}
		ctx.agent->activate_skill("grill-me");
	}
};

extern "C" {

const char *plugin_name(void)
{
	return "Matt Pocock Skills";
}

const char *plugin_description(void)
{
	return "Provides productivity skills from Matt Pocock's repository, including the interactive /grill-me command.";
}

void plugin_run(void)
{
	// Raw content of the grill-me skill
	std::string grill_me_skill = R"---(---
name: grill-me
description: Interview the user relentlessly about a plan or design until reaching shared understanding, resolving each branch of the decision tree. Use when user wants to stress-test a plan, get grilled on their design, or mentions "grill me".
---

Interview me relentlessly about every aspect of this plan until we reach a shared understanding. Walk down each branch of the design tree, resolving dependencies between decisions one-by-one. For each question, provide your recommended answer.

Ask the questions one at a time.

If a question can be answered by exploring the codebase, explore the codebase instead.
)---";

	// 1. Register the grill-me skill (hidden/invisible by default)
	agentlib::skill_manager::get_instance().register_skill(grill_me_skill, false);

	// 2. Register the /grill-me slash command
	command_registry::get_instance().register_command(std::make_unique<grill_me_command>());
}

void plugin_unload(void)
{
	// 1. Unregister the /grill-me slash command
	command_registry::get_instance().unregister_command("grill-me");

	// 2. Unregister the grill-me skill
	agentlib::skill_manager::get_instance().unregister_skill("grill-me");
}

}
