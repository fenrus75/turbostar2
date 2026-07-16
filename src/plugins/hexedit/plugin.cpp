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
void register_hexinspect(void);
void unregister_hexinspect(void);

void plugin_run(void)
{
	register_hexdump();
	register_hexwrite();
	register_hexinspect();
	agentlib::tool_registry::get_instance().register_tool_family(
		"hexedit",
		"Activate when viewing or writing raw hex data in binary/text files",
		"The 'hexedit' tool family allows you to inspect and modify raw byte data in binary or text files.\n\n"
		"Key Tools:\n"
		"- hexdump: Displays structural binary contents formatted as a hex dump.\n"
		"- hexwrite: Modifies specific byte ranges in a file by writing a sequence of raw hex values.\n"
		"- hexinspect: Performs specific structure-aware patterns or offset inspections."
	);
}

void plugin_unload(void)
{
	unregister_hexdump();
	unregister_hexwrite();
	unregister_hexinspect();
	agentlib::tool_registry::get_instance().unregister_tool_family("hexedit");
}

}
