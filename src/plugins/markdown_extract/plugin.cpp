#include "agentlib/tool_registry.h"
#include "agentlib/subagent_manager.h"
#include "markdown_extract_agent.h"

extern "C" {

void register_markdown_extract(void);
void unregister_markdown_extract(void);

const char *plugin_name(void)
{
	return "Markdown Extract Plugin";
}

const char *plugin_description(void)
{
	return "Provides tools and subagents for structure-aware extraction of targeted information from Markdown documents and VFS manpages.";
}

void plugin_run(void)
{
	register_markdown_extract();
	agentlib::subagent_manager::get_instance().register_subagent("markdown_extractor", markdown_extract_agent_md);
}

void plugin_unload(void)
{
	unregister_markdown_extract();
	agentlib::subagent_manager::get_instance().unregister_subagent("markdown_extractor");
}

}
