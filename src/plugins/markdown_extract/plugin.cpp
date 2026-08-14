#include "agentlib/tool_registry.h"
#include "agentlib/subagent_manager.h"
#include "markdown_extract_agent.h"

namespace tools {
void register_markdown_extract(void);
void unregister_markdown_extract(void);
}

extern "C" {

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
	tools::register_markdown_extract();
	agentlib::subagent_manager::get_instance().register_subagent("markdown_extractor", markdown_extract_agent_md);
	agentlib::tool_registry::get_instance().register_tool_family(
		"markdown_extract",
		"Activate when extracting targeted information, directives, or section details from Markdown documents or VFS manpages",
		"The 'markdown_extract' tool family provides structure-aware extraction tools for Markdown documents.\n\n"
		"Key Tools:\n"
		"- markdown_extract: Dispatches a subagent to extract specific sections or directives (e.g. ProtectKernelTunables) with full section context."
	);
}

void plugin_unload(void)
{
	tools::unregister_markdown_extract();
	agentlib::subagent_manager::get_instance().unregister_subagent("markdown_extractor");
	agentlib::tool_registry::get_instance().unregister_tool_family("markdown_extract");
}

}
