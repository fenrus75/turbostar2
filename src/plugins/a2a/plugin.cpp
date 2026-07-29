#include "agentlib/tool_registry.h"

extern "C" {

const char *plugin_name(void)
{
	return "A2A Tools";
}

const char *plugin_description(void)
{
	return "Provides tools for Agent-to-Agent (A2A) protocol card validation and publishing.";
}

void register_a2a_validate_card(void);
void unregister_a2a_validate_card(void);

void plugin_run(void)
{
	register_a2a_validate_card();
	agentlib::tool_registry::get_instance().register_tool_family(
		"a2a",
		"Activate when working with Agent-to-Agent (A2A) cards, protocol endpoints, and inter-agent communication",
		"The 'a2a' tool family provides tools for Agent-to-Agent protocol interactions.\n\n"
		"Key Tools:\n"
		"- a2a_validate_card: Validates an A2A Agent Card JSON file or raw string against the formal A2A specification."
	);
}

void plugin_unload(void)
{
	unregister_a2a_validate_card();
	agentlib::tool_registry::get_instance().unregister_tool_family("a2a");
}

}
