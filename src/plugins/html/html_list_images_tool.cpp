#include "plugins/html/html_list_images_tool.h"
#include <lexbor/html/parser.h>
#include <lexbor/dom/interfaces/element.h>
#include <lexbor/dom/dom.h>
#include "markdown_utils.h"
#include <fstream>
#include <sstream>

namespace tools
{

namespace
{

std::string trim(const std::string &s)
{
	size_t start = s.find_first_not_of(" \t\r\n");
	if (start == std::string::npos)
		return "";
	size_t end = s.find_last_not_of(" \t\r\n");
	return s.substr(start, end - start + 1);
}

std::string sanitize_value(const std::string &raw)
{
	std::string result;
	for (char c : raw) {
		if (c == '|') {
			result += "\\|";
		} else if (c == '\n' || c == '\r' || c == '\t') {
			result += " ";
		} else {
			result += c;
		}
	}
	return trim(result);
}

void find_images(lxb_html_document_t * /*document*/, lxb_dom_node_t *node, std::vector<std::pair<std::string, std::string>> &images)
{
	if (!node)
		return;

	if (lxb_dom_node_tag_id(node) == LXB_TAG_IMG) {
		size_t src_len = 0;
		const lxb_char_t *src_val = lxb_dom_element_get_attribute(
		    lxb_dom_interface_element(node),
		    reinterpret_cast<const lxb_char_t *>("src"), 3, &src_len);
		if (src_val) {
			std::string src(reinterpret_cast<const char *>(src_val), src_len);

			size_t alt_len = 0;
			const lxb_char_t *alt_val = lxb_dom_element_get_attribute(
			    lxb_dom_interface_element(node),
			    reinterpret_cast<const lxb_char_t *>("alt"), 3, &alt_len);
			std::string alt;
			if (alt_val) {
				alt = std::string(reinterpret_cast<const char *>(alt_val), alt_len);
			}

			images.push_back({sanitize_value(alt), sanitize_value(src)});
		}
	}

	for (lxb_dom_node_t *child = lxb_dom_node_first_child(node); child != nullptr; child = lxb_dom_node_next(child)) {
		find_images(nullptr, child, images);
	}
}

} // namespace

html_list_images_tool::html_list_images_tool(html_list_images_args args)
    : llm_tool_action("Listing HTML images"), args_(std::move(args))
{
}

bool html_list_images_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string html_list_images_tool::execute(agentlib::tool_context & /*ctx*/)
{
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

	std::vector<std::pair<std::string, std::string>> images;
	find_images(document, body_node, images);

	lxb_html_document_destroy(document);

	if (images.empty()) {
		return "No images found in the HTML file.";
	}

	std::string tbl = "### Images extracted from: " + args_.requested_path + "\n\n";
	tbl += "| Alt Text | Image URL |\n| --- | --- |\n";
	for (const auto &img : images) {
		tbl += "| " + (img.first.empty() ? "*(no alt text)*" : img.first) + " | " + img.second + " |\n";
	}

	return markdown_utils::align_all_tables(tbl, false);
}

} // namespace tools
