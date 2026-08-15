#include "plugins/html/html_list_images_tool.h"
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

void find_images(lxb_html_document_t * /*document*/, lxb_dom_node_t *node, std::vector<std::pair<std::string, std::string>> &images, int depth = 0)
{
	if (!node || depth > 64 || images.size() >= 1000)
		return;

	if (lxb_dom_node_type(node) == LXB_DOM_NODE_TYPE_ELEMENT && lxb_dom_node_tag_id(node) == LXB_TAG_IMG) {
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

			std::string safe_src;
			if (src.starts_with("data:")) {
				safe_src = "[data-uri-image-omitted]";
			} else {
				safe_src = html_sanitize::sanitize_link_url(src);
			}

			images.push_back({html_sanitize::sanitize_markdown_cell(alt), safe_src});
		}
	}

	for (lxb_dom_node_t *child = lxb_dom_node_first_child(node); child != nullptr; child = lxb_dom_node_next(child)) {
		find_images(nullptr, child, images, depth + 1);
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

	std::vector<std::pair<std::string, std::string>> images;
	find_images(document, body_node, images);

	lxb_html_document_destroy(document);

	if (images.empty()) {
		return "No images found in the HTML file.";
	}

	std::string tbl = "### Images extracted from: " + html_sanitize::sanitize_markdown_cell(args_.requested_path) + "\n\n";
	tbl += "| Alt Text | Image URL |\n| --- | --- |\n";
	for (const auto &img : images) {
		tbl += "| " + (img.first.empty() ? "*(no alt text)*" : img.first) + " | " + img.second + " |\n";
	}

	std::string aligned = markdown_utils::align_all_tables(tbl, false);
	return fs_utils::wrap_prompt_untrusted_data_tag("extracted_images", aligned);
}

} // namespace tools
