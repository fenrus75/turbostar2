#include "agentlib/tool_registry.h"
#include "agentlib/filter_registry.h"
#include "plugins/html/html_extract_text_tool.h"

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
void register_html_extract_text(void);
void unregister_html_extract_text(void);

void plugin_run(void)
{
	register_html_extract_tables();
	register_html_list_links();
	register_html_list_images();
	register_html_extract_text();
	agentlib::filter_registry::get_instance().register_filter("html_to_markdown", [](const std::string &html_content) {
		return html::convert_to_markdown(html_content, true);
	});
	agentlib::filter_registry::get_instance().register_filter("html_to_markdown_plain", [](const std::string &html_content) {
		return html::convert_to_markdown(html_content, false);
	});
	agentlib::filter_registry::get_instance().register_filter("html_extract_tables", [](const std::string &html_content) {
		return html::extract_tables(html_content);
	});
	agentlib::tool_registry::get_instance().register_tool_family("html", "Activate when extracting data, tables, or info from HTML documents");
}

void plugin_unload(void)
{
	unregister_html_extract_tables();
	unregister_html_list_links();
	unregister_html_list_images();
	unregister_html_extract_text();
	agentlib::filter_registry::get_instance().unregister_filter("html_to_markdown");
	agentlib::filter_registry::get_instance().unregister_filter("html_to_markdown_plain");
	agentlib::filter_registry::get_instance().unregister_filter("html_extract_tables");
	agentlib::tool_registry::get_instance().unregister_tool_family("html");
}

}
