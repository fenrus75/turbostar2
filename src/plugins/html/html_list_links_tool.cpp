#include "plugins/html/html_list_links_tool.h"
#include <lexbor/html/parser.h>
#include <lexbor/dom/interfaces/element.h>
#include <lexbor/dom/dom.h>
#include "markdown_utils.h"
#include "plugins/html/html_sanitize.h"
#include "fs_utils.h"
#include <fstream>
#include <sstream>

namespace tools
{

namespace
{

void find_links(lxb_html_document_t *document, lxb_dom_node_t *node, std::vector<std::pair<std::string, std::string>> &links, int depth = 0)
{
	if (!node || depth > 64 || links.size() >= 1000)
		return;

	if (lxb_dom_node_type(node) == LXB_DOM_NODE_TYPE_ELEMENT && lxb_dom_node_tag_id(node) == LXB_TAG_A) {
		size_t value_len = 0;
		const lxb_char_t *href_val = lxb_dom_element_get_attribute(
		    lxb_dom_interface_element(node),
		    reinterpret_cast<const lxb_char_t *>("href"), 4, &value_len);
		if (href_val) {
			std::string href(reinterpret_cast<const char *>(href_val), value_len);

			size_t text_len = 0;
			lxb_char_t *text_val = lxb_dom_node_text_content(node, &text_len);
			std::string text;
			if (text_val) {
				text = std::string(reinterpret_cast<char *>(text_val), text_len);
				lxb_dom_document_destroy_text(lxb_dom_interface_document(document), text_val);
			}

			links.push_back({html_sanitize::sanitize_markdown_cell(text), html_sanitize::sanitize_link_url(href)});
		}
	}

	for (lxb_dom_node_t *child = lxb_dom_node_first_child(node); child != nullptr; child = lxb_dom_node_next(child)) {
		find_links(document, child, links, depth + 1);
	}
}

} // namespace

html_list_links_tool::html_list_links_tool(html_list_links_args args)
    : llm_tool_action("Listing HTML links"), args_(std::move(args))
{
}

bool html_list_links_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string html_list_links_tool::execute(agentlib::tool_context & /*ctx*/)
{
	if (!fs_utils::is_regular_file(args_.safe_path)) {
		return "Error: Target path is not a regular file.";
	}

	std::ifstream ifs(args_.safe_path, std::ios::binary);
	if (!ifs) {
		return "Error: Unable to open file " + args_.requested_path;
	}

	std::string html_content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
	ifs.close();

	lxb_html_document_t *document = lxb_html_document_create();
	if (!document) {
		return "Error: Failed to initialize HTML parser.";
	}

	lxb_status_t status = lxb_html_document_parse(document,
						      reinterpret_cast<const lxb_char_t *>(html_content.data()),
						      html_content.size());
	if (status != LXB_STATUS_OK) {
		lxb_html_document_destroy(document);
		return "Error: Failed to parse HTML content.";
	}

	lxb_dom_node_t *body_node = nullptr;
	if (document->body) {
		body_node = lxb_dom_interface_node(document->body);
	} else {
		body_node = lxb_dom_interface_node(lxb_dom_document_root(lxb_dom_interface_document(document)));
	}

	std::vector<std::pair<std::string, std::string>> links;
	find_links(document, body_node, links);

	lxb_html_document_destroy(document);

	if (links.empty()) {
		return "No links found in the HTML file.";
	}

	std::string tbl = "### Links extracted from: " + html_sanitize::sanitize_markdown_cell(args_.requested_path) + "\n\n";
	tbl += "| Text | URL |\n| --- | --- |\n";
	for (const auto &link : links) {
		tbl += "| " + (link.first.empty() ? "*(no text)*" : link.first) + " | " + link.second + " |\n";
	}

	std::string aligned = markdown_utils::align_all_tables(tbl, false);
	return fs_utils::wrap_prompt_untrusted_data_tag("extracted_links", aligned);
}

} // namespace tools
