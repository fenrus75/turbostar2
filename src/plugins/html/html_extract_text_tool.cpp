#include "plugins/html/html_extract_text_tool.h"
#include "plugins/html/html_sanitize.h"
#include "fs_utils.h"
#include <lexbor/html/parser.h>
#include <lexbor/dom/interfaces/element.h>
#include <lexbor/dom/dom.h>
#include "markdown_utils.h"
#include <fstream>
#include <sstream>
#include <algorithm>

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

std::string sanitize_cell(const std::string &raw)
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

std::string collapse_whitespace(const std::string &s)
{
	std::string result;
	bool last_was_space = false;
	for (char c : s) {
		if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
			if (!last_was_space) {
				result += ' ';
				last_was_space = true;
			}
		} else {
			result += c;
			last_was_space = false;
		}
	}
	return result;
}

void collect_rows(lxb_dom_node_t *node, std::vector<lxb_dom_node_t *> &rows)
{
	for (lxb_dom_node_t *child = lxb_dom_node_first_child(node); child != nullptr; child = lxb_dom_node_next(child)) {
		if (lxb_dom_node_type(child) == LXB_DOM_NODE_TYPE_ELEMENT) {
			lxb_tag_id_t tag = lxb_dom_node_tag_id(child);
			if (tag == LXB_TAG_TR) {
				rows.push_back(child);
			} else if (tag == LXB_TAG_TABLE) {
				continue;
			} else {
				collect_rows(child, rows);
			}
		} else {
			collect_rows(child, rows);
		}
	}
}

void collect_cells(lxb_dom_node_t *node, std::vector<lxb_dom_node_t *> &cells)
{
	for (lxb_dom_node_t *child = lxb_dom_node_first_child(node); child != nullptr; child = lxb_dom_node_next(child)) {
		if (lxb_dom_node_type(child) == LXB_DOM_NODE_TYPE_ELEMENT) {
			lxb_tag_id_t tag = lxb_dom_node_tag_id(child);
			if (tag == LXB_TAG_TH || tag == LXB_TAG_TD) {
				cells.push_back(child);
			} else if (tag == LXB_TAG_TABLE) {
				continue;
			} else {
				collect_cells(child, cells);
			}
		} else {
			collect_cells(child, cells);
		}
	}
}

void process_table(lxb_html_document_t *document, lxb_dom_node_t *table_node, std::string &output)
{
	std::string caption_text;
	for (lxb_dom_node_t *child = lxb_dom_node_first_child(table_node); child != nullptr; child = lxb_dom_node_next(child)) {
		if (lxb_dom_node_type(child) == LXB_DOM_NODE_TYPE_ELEMENT && lxb_dom_node_tag_id(child) == LXB_TAG_CAPTION) {
			size_t len = 0;
			lxb_char_t *text = lxb_dom_node_text_content(child, &len);
			if (text) {
				caption_text = trim(std::string(reinterpret_cast<char *>(text), len));
				lxb_dom_document_destroy_text(lxb_dom_interface_document(document), text);
			}
			break;
		}
	}

	std::vector<lxb_dom_node_t *> rows;
	collect_rows(table_node, rows);
	if (rows.empty()) {
		return;
	}

	std::vector<std::vector<std::string>> grid;
	size_t max_cols = 0;
	for (auto *row : rows) {
		std::vector<lxb_dom_node_t *> cells;
		collect_cells(row, cells);

		std::vector<std::string> row_values;
		for (auto *cell : cells) {
			size_t len = 0;
			lxb_char_t *text = lxb_dom_node_text_content(cell, &len);
			std::string val;
			if (text) {
				val = std::string(reinterpret_cast<char *>(text), len);
				lxb_dom_document_destroy_text(lxb_dom_interface_document(document), text);
			}
			row_values.push_back(sanitize_cell(val));
		}
		max_cols = std::max(max_cols, row_values.size());
		grid.push_back(row_values);
	}

	if (max_cols == 0) {
		return;
	}

	for (auto &row : grid) {
		while (row.size() < max_cols) {
			row.push_back("");
		}
	}

	std::string table_md;
	if (!caption_text.empty()) {
		table_md += "\n**Table: " + caption_text + "**\n\n";
	}

	for (size_t col = 0; col < max_cols; ++col) {
		table_md += "| " + grid[0][col] + " ";
	}
	table_md += "|\n";

	for (size_t col = 0; col < max_cols; ++col) {
		table_md += "| --- ";
	}
	table_md += "|\n";

	for (size_t r = 1; r < grid.size(); ++r) {
		for (size_t col = 0; col < max_cols; ++col) {
			table_md += "| " + grid[r][col] + " ";
		}
		table_md += "|\n";
	}

	output += table_md;
}

void append_newline(std::string &out)
{
	if (!out.empty() && out.back() != '\n') {
		out += "\n";
	}
}

void append_paragraph_separator(std::string &out)
{
	if (out.empty())
		return;
	if (out.back() == '\n') {
		if (out.size() >= 2 && out[out.size() - 2] != '\n') {
			out += "\n";
		}
	} else {
		out += "\n\n";
	}
}

std::string extract_base_url(lxb_html_document_t *document)
{
	if (!document)
		return "";

	lxb_dom_node_t *root = lxb_dom_interface_node(lxb_dom_document_root(lxb_dom_interface_document(document)));
	if (!root)
		return "";

	lxb_dom_collection_t *collection = lxb_dom_collection_make(&document->dom_document, 16);
	if (!collection)
		return "";

	std::string base_url;

	lxb_dom_elements_by_tag_name(lxb_dom_interface_element(root), collection, reinterpret_cast<const lxb_char_t *>("base"), 4);
	for (size_t i = 0; i < lxb_dom_collection_length(collection); ++i) {
		lxb_dom_element_t *element = lxb_dom_collection_element(collection, i);
		size_t len = 0;
		const lxb_char_t *val = lxb_dom_element_get_attribute(element, reinterpret_cast<const lxb_char_t *>("href"), 4, &len);
		if (val && len > 0) {
			base_url = std::string(reinterpret_cast<const char *>(val), len);
			break;
		}
	}

	if (base_url.empty()) {
		lxb_dom_collection_clean(collection);
		lxb_dom_elements_by_tag_name(lxb_dom_interface_element(root), collection, reinterpret_cast<const lxb_char_t *>("link"), 4);
		for (size_t i = 0; i < lxb_dom_collection_length(collection); ++i) {
			lxb_dom_element_t *element = lxb_dom_collection_element(collection, i);
			size_t rel_len = 0;
			const lxb_char_t *rel_val = lxb_dom_element_get_attribute(element, reinterpret_cast<const lxb_char_t *>("rel"), 3, &rel_len);
			if (rel_val && rel_len > 0) {
				std::string rel_str(reinterpret_cast<const char *>(rel_val), rel_len);
				if (rel_str == "canonical") {
					size_t href_len = 0;
					const lxb_char_t *href_val = lxb_dom_element_get_attribute(element, reinterpret_cast<const lxb_char_t *>("href"), 4, &href_len);
					if (href_val && href_len > 0) {
						base_url = std::string(reinterpret_cast<const char *>(href_val), href_len);
						break;
					}
				}
			}
		}
	}

	lxb_dom_collection_destroy(collection, true);

	if (!base_url.empty()) {
		size_t scheme_end = base_url.find("://");
		if (scheme_end != std::string::npos) {
			size_t host_end = base_url.find('/', scheme_end + 3);
			if (host_end != std::string::npos) {
				return base_url.substr(0, host_end);
			}
			return base_url;
		}
	}

	return "";
}

void walk_node(lxb_html_document_t *document, lxb_dom_node_t *node, std::string &out, bool rich, int &list_depth,
	       std::vector<int> &list_counters, bool &in_main_content, const std::string &base_url, int &nav_container_depth, int depth = 0)
{
	if (!node || depth > 64 || out.size() >= 32768)
		return;

	lxb_dom_node_type_t type = lxb_dom_node_type(node);
	if (type == LXB_DOM_NODE_TYPE_TEXT) {
		if (nav_container_depth > 0) {
			return;
		}
		size_t len = 0;
		lxb_char_t *text = lxb_dom_node_text_content(node, &len);
		if (text) {
			std::string raw(reinterpret_cast<char *>(text), len);
			lxb_dom_document_destroy_text(lxb_dom_interface_document(document), text);
			out += collapse_whitespace(raw);
		}
		return;
	}

	lxb_tag_id_t tag = LXB_TAG__UNDEF;
	bool is_nav_container = false;
	if (type == LXB_DOM_NODE_TYPE_ELEMENT) {
		tag = lxb_dom_node_tag_id(node);
		lxb_dom_element_t *elem = lxb_dom_interface_element(node);

		size_t id_len = 0, class_len = 0;
		const lxb_char_t *id_val = lxb_dom_element_get_attribute(elem, reinterpret_cast<const lxb_char_t *>("id"), 2, &id_len);
		const lxb_char_t *class_val = lxb_dom_element_get_attribute(elem, reinterpret_cast<const lxb_char_t *>("class"), 5, &class_len);
		std::string attr_str;
		if (id_val && id_len > 0)
			attr_str += std::string(reinterpret_cast<const char *>(id_val), id_len) + " ";
		if (class_val && class_len > 0)
			attr_str += std::string(reinterpret_cast<const char *>(class_val), class_len);
		std::transform(attr_str.begin(), attr_str.end(), attr_str.begin(), [](unsigned char c) { return std::tolower(c); });

		std::string_view id_sv(id_val ? reinterpret_cast<const char *>(id_val) : "", id_len);

		if (tag == LXB_TAG_NAV || tag == LXB_TAG_HEADER || tag == LXB_TAG_FOOTER || tag == LXB_TAG_ASIDE ||
		    attr_str.find("sidebar") != std::string::npos || attr_str.find("vector-menu") != std::string::npos ||
		    attr_str.find("vector-toc") != std::string::npos || attr_str.find("catlinks") != std::string::npos ||
		    attr_str.find("printfooter") != std::string::npos || attr_str.find("mw-panel") != std::string::npos ||
		    attr_str.find("vector-header") != std::string::npos || attr_str.find("mw-navigation") != std::string::npos ||
		    id_sv == "mw-head" || id_sv == "toc" || id_sv == "footer") {
			is_nav_container = true;
			nav_container_depth++;
			if (tag == LXB_TAG_FOOTER || id_sv == "footer" || attr_str.find("catlinks") != std::string::npos) {
				in_main_content = false;
			}
		}
	}

	struct nav_guard {
		int &depth;
		bool active;
		~nav_guard()
		{
			if (active)
				depth--;
		}
	} guard{nav_container_depth, is_nav_container};

	// Skip non-visible tags
	if (tag == LXB_TAG_SCRIPT || tag == LXB_TAG_STYLE || tag == LXB_TAG_HEAD || tag == LXB_TAG_NOSCRIPT || tag == LXB_TAG_IFRAME) {
		return;
	}

	// 1. Headings
	int heading_level = 0;
	if (tag == LXB_TAG_H1)
		heading_level = 1;
	else if (tag == LXB_TAG_H2)
		heading_level = 2;
	else if (tag == LXB_TAG_H3)
		heading_level = 3;
	else if (tag == LXB_TAG_H4)
		heading_level = 4;
	else if (tag == LXB_TAG_H5)
		heading_level = 5;
	else if (tag == LXB_TAG_H6)
		heading_level = 6;

	if (heading_level > 0 || tag == LXB_TAG_MAIN || tag == LXB_TAG_ARTICLE) {
		in_main_content = true;
	}

	if (heading_level > 0) {
		if (nav_container_depth > 0) {
			return;
		}
		append_paragraph_separator(out);
		out += std::string(heading_level, '#') + " ";
		for (lxb_dom_node_t *child = lxb_dom_node_first_child(node); child != nullptr; child = lxb_dom_node_next(child)) {
			walk_node(document, child, out, rich, list_depth, list_counters, in_main_content, base_url, nav_container_depth);
		}
		append_paragraph_separator(out);
		return;
	}

	// 2. Preformatted / code blocks
	if (tag == LXB_TAG_PRE) {
		size_t len = 0;
		lxb_char_t *text = lxb_dom_node_text_content(node, &len);
		if (text) {
			append_paragraph_separator(out);
			out += "```\n" + std::string(reinterpret_cast<char *>(text), len) + "\n```";
			append_paragraph_separator(out);
			lxb_dom_document_destroy_text(lxb_dom_interface_document(document), text);
		}
		return;
	}

	// 3. Tables
	if (tag == LXB_TAG_TABLE) {
		std::string table_md;
		process_table(document, node, table_md);
		append_paragraph_separator(out);
		out += table_md;
		append_paragraph_separator(out);
		return;
	}

	// 4. Lists
	if (tag == LXB_TAG_UL || tag == LXB_TAG_OL) {
		append_paragraph_separator(out);
		list_depth++;
		if (tag == LXB_TAG_OL) {
			list_counters.push_back(1);
		} else {
			list_counters.push_back(0); // 0 indicates unordered
		}

		for (lxb_dom_node_t *child = lxb_dom_node_first_child(node); child != nullptr; child = lxb_dom_node_next(child)) {
			walk_node(document, child, out, rich, list_depth, list_counters, in_main_content, base_url, nav_container_depth);
		}

		list_counters.pop_back();
		list_depth--;
		append_paragraph_separator(out);
		return;
	}

	if (tag == LXB_TAG_LI) {
		append_newline(out);
		if (list_depth > 0 && !list_counters.empty()) {
			out += std::string((list_depth - 1) * 2, ' ');
			int count = list_counters.back();
			if (count > 0) {
				out += std::to_string(count) + ". ";
				list_counters.back() = count + 1;
			} else {
				out += "- ";
			}
		} else {
			out += "- ";
		}

		for (lxb_dom_node_t *child = lxb_dom_node_first_child(node); child != nullptr; child = lxb_dom_node_next(child)) {
			walk_node(document, child, out, rich, list_depth, list_counters, in_main_content, base_url, nav_container_depth);
		}
		append_newline(out);
		return;
	}

	// 5. Paragraphs & Block Containers
	if (tag == LXB_TAG_P) {
		append_paragraph_separator(out);
		for (lxb_dom_node_t *child = lxb_dom_node_first_child(node); child != nullptr; child = lxb_dom_node_next(child)) {
			walk_node(document, child, out, rich, list_depth, list_counters, in_main_content, base_url, nav_container_depth);
		}
		append_paragraph_separator(out);
		return;
	}

	if (tag == LXB_TAG_DIV || tag == LXB_TAG_SECTION || tag == LXB_TAG_ARTICLE || tag == LXB_TAG_HEADER || tag == LXB_TAG_FOOTER) {
		append_newline(out);
		for (lxb_dom_node_t *child = lxb_dom_node_first_child(node); child != nullptr; child = lxb_dom_node_next(child)) {
			walk_node(document, child, out, rich, list_depth, list_counters, in_main_content, base_url, nav_container_depth);
		}
		append_newline(out);
		return;
	}

	if (tag == LXB_TAG_BR) {
		append_newline(out);
		return;
	}

	// 6. Hyperlinks
	if (tag == LXB_TAG_A) {
		size_t href_len = 0;
		const lxb_char_t *href_val = lxb_dom_element_get_attribute(
		    lxb_dom_interface_element(node),
		    reinterpret_cast<const lxb_char_t *>("href"), 4, &href_len);
		if (href_val && href_len > 0) {
			std::string link_text;
			for (lxb_dom_node_t *child = lxb_dom_node_first_child(node); child != nullptr; child = lxb_dom_node_next(child)) {
				walk_node(document, child, link_text, rich, list_depth, list_counters, in_main_content, base_url, nav_container_depth);
			}
			link_text = collapse_whitespace(link_text);
			link_text = trim(link_text);

			std::string lower_text = link_text;
			std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(), [](unsigned char c) { return std::tolower(c); });
			if (lower_text == "edit" || lower_text == "[edit]" || lower_text == "edit source" || lower_text == "[edit source]" || lower_text == "edit links") {
				return;
			}

			if (link_text.empty()) {
				return;
			}

			// If inside explicit UI navigation container, strip link completely!
			if (nav_container_depth > 0) {
				return;
			}

			if (!in_main_content) {
				out += link_text;
				return;
			}

			std::string href(reinterpret_cast<const char *>(href_val), href_len);
			if (href.starts_with("//")) {
				href = "https:" + href;
			} else if (href.starts_with("/") && !base_url.empty()) {
				if (base_url.starts_with("http://") || base_url.starts_with("https://")) {
					href = base_url + href;
				}
			}

			std::string safe_url = html_sanitize::sanitize_link_url(href);
			std::string safe_text = html_sanitize::sanitize_markdown_cell(link_text);
			out += "[" + safe_text + "](" + safe_url + ")";
			return;
		}
	}

	// 7. Inline styles
	bool is_bold = (tag == LXB_TAG_STRONG || tag == LXB_TAG_B);
	bool is_italic = (tag == LXB_TAG_EM || tag == LXB_TAG_I);
	bool is_code = (tag == LXB_TAG_CODE);

	if (is_bold && rich)
		out += "**";
	if (is_italic && rich)
		out += "*";
	if (is_code)
		out += "`";

	for (lxb_dom_node_t *child = lxb_dom_node_first_child(node); child != nullptr; child = lxb_dom_node_next(child)) {
		walk_node(document, child, out, rich, list_depth, list_counters, in_main_content, base_url, nav_container_depth, depth + 1);
	}

	if (is_bold && rich)
		out += "**";
	if (is_italic && rich)
		out += "*";
	if (is_code)
		out += "`";
}

} // namespace

html_extract_text_tool::html_extract_text_tool(html_extract_text_args args)
    : llm_tool_action("Extracting text from HTML"), args_(std::move(args))
{
}

bool html_extract_text_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string html_extract_text_tool::execute(agentlib::tool_context & /*ctx*/)
{
	if (!fs_utils::is_regular_file(args_.safe_path)) {
		return "Error: Target path is not a regular file.";
	}
	std::error_code ec;
	auto sz = std::filesystem::file_size(args_.safe_path, ec);
	if (ec || sz > 5 * 1024 * 1024) {
		return "Error: Target file exceeds 5MB size limit.";
	}

	std::ifstream ifs(args_.safe_path, std::ios::binary);
	if (!ifs) {
		return "Error: Unable to open file " + args_.requested_path;
	}

	std::string html_content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
	ifs.close();

	std::string result = html::convert_to_markdown(html_content, args_.rich);
	return fs_utils::wrap_prompt_untrusted_data_tag("extracted_text", result);
}

} // namespace tools

namespace html
{

void find_tables_local(lxb_dom_node_t *node, std::vector<lxb_dom_node_t *> &tables)
{
	if (!node)
		return;

	if (lxb_dom_node_type(node) == LXB_DOM_NODE_TYPE_ELEMENT && lxb_dom_node_tag_id(node) == LXB_TAG_TABLE) {
		tables.push_back(node);
		return;
	}

	for (lxb_dom_node_t *child = lxb_dom_node_first_child(node); child != nullptr; child = lxb_dom_node_next(child)) {
		find_tables_local(child, tables);
	}
}

std::string extract_tables(const std::string &html_content)
{
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

	std::vector<lxb_dom_node_t *> tables;
	find_tables_local(body_node, tables);

	if (tables.empty()) {
		lxb_html_document_destroy(document);
		return "No tables found in the HTML file.";
	}

	std::string tbl_output;
	for (auto *table : tables) {
		tools::process_table(document, table, tbl_output);
		tbl_output += "\n";
	}

	lxb_html_document_destroy(document);

	return markdown_utils::align_all_tables(tools::trim(tbl_output), false);
}

std::string convert_to_markdown(const std::string &html_content, bool rich)
{
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

	std::string base_url = tools::extract_base_url(document);
	std::string extracted;
	int list_depth = 0;
	std::vector<int> list_counters;
	bool in_main_content = false;
	int nav_container_depth = 0;
	tools::walk_node(document, body_node, extracted, rich, list_depth, list_counters, in_main_content, base_url, nav_container_depth);

	lxb_html_document_destroy(document);

	// Post-processing cleanup: trim trailing whitespace and collapse consecutive blank lines
	std::string final_out;
	std::stringstream ss(extracted);
	std::string line;
	int blank_lines = 0;
	while (std::getline(ss, line)) {
		size_t end = line.find_last_not_of(" \t\r\n");
		if (end == std::string::npos) {
			line = "";
		} else {
			line = line.substr(0, end + 1);
		}

		std::string trimmed = tools::trim(line);
		if (trimmed == "[]" || trimmed == "[ ]" || trimmed == "-" || trimmed == "*") {
			line = "";
		}

		if (line.empty()) {
			blank_lines++;
			if (blank_lines <= 1) {
				final_out += "\n";
			}
		} else {
			blank_lines = 0;
			final_out += line + "\n";
		}
	}

	return markdown_utils::align_all_tables(tools::trim(final_out), false);
}

} // namespace html
