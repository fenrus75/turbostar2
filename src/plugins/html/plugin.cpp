#include "agentlib/tool_registry.h"

extern "C" {

const char *plugin_name(void)
{
	return "HTML Tools";
}

const char *plugin_description(void)
{
	return "Provides HTML document processing tools, including tables extraction (html_extract_tables).";
}

void register_html_extract_tables(void);
void unregister_html_extract_tables(void);
void register_html_list_links(void);
void unregister_html_list_links(void);
void register_html_list_images(void);
void unregister_html_list_images(void);

void plugin_run(void)
{
	register_html_extract_tables();
	register_html_list_links();
	register_html_list_images();
	agentlib::tool_registry::get_instance().register_tool_family("html", "Activate when extracting data, tables, or info from HTML documents");
}

void plugin_unload(void)
{
	unregister_html_extract_tables();
	unregister_html_list_links();
	unregister_html_list_images();
	agentlib::tool_registry::get_instance().unregister_tool_family("html");
}

}
