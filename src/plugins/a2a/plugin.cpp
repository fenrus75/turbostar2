#include "agentlib/tool_registry.h"
#include "agentlib/subagent_manager.h"
#include "a2acardgenerator_agent.h"

extern "C" {

const char *plugin_name(void)
{
	return "A2A Tools";
}

const char *plugin_description(void)
{
	return "Provides tools and subagents for Agent-to-Agent (A2A) protocol card synthesis, validation, and publishing.";
}

void register_a2a_validate_card(void);
void unregister_a2a_validate_card(void);
void register_a2a_generate_card_with_agent(void);
void unregister_a2a_generate_card_with_agent(void);

void plugin_run(void)
{
	register_a2a_validate_card();
	register_a2a_generate_card_with_agent();
	agentlib::subagent_manager::get_instance().register_subagent("a2acardgenerator", a2acardgenerator_agent_md);
	agentlib::tool_registry::get_instance().register_tool_family(
		"a2a",
		"Activate when working with Agent-to-Agent (A2A) cards, protocol endpoints, and inter-agent communication",
		"The 'a2a' tool family provides tools for Agent-to-Agent protocol interactions.\n\n"
		"Key Tools:\n"
		"- a2a_validate_card: Validates an A2A Agent Card JSON file or raw string against the formal A2A specification.\n"
		"- a2a_generate_card_with_agent: Dispatches the a2acardgenerator subagent to synthesize a validated .card.json from an agent .md definition."
	);
}

void plugin_unload(void)
{
	unregister_a2a_validate_card();
	unregister_a2a_generate_card_with_agent();
	agentlib::subagent_manager::get_instance().unregister_subagent("a2acardgenerator");
	agentlib::tool_registry::get_instance().unregister_tool_family("a2a");
}

}
