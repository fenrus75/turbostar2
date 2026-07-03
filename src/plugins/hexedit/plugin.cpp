#include "agentlib/tool_registry.h"

extern "C" {

const char *plugin_name(void)
{
	return "Hex Editor Tools";
}

const char *plugin_description(void)
{
	return "Provides structural hexdump inspection and raw byte patching tools (hexdump, hexwrite).";
}

void register_hexdump(void);
void unregister_hexdump(void);
void register_hexwrite(void);
void unregister_hexwrite(void);

void plugin_run(void)
{
	register_hexdump();
	register_hexwrite();
	agentlib::tool_registry::get_instance().register_tool_family("hexedit", "Activate when viewing or writing raw hex data in binary/text files");
}

void plugin_unload(void)
{
	unregister_hexdump();
	unregister_hexwrite();
	agentlib::tool_registry::get_instance().unregister_tool_family("hexedit");
}

}
