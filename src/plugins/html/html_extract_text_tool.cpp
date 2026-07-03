#include "plugins/html/html_extract_text_tool.h"
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
		lxb_tag_id_t tag = lxb_dom_node_tag_id(child);
		if (tag == LXB_TAG_TR) {
			rows.push_back(child);
		} else if (tag == LXB_TAG_TABLE) {
			continue;
		} else {
			collect_rows(child, rows);
		}
	}
}

void collect_cells(lxb_dom_node_t *node, std::vector<lxb_dom_node_t *> &cells)
{
	for (lxb_dom_node_t *child = lxb_dom_node_first_child(node); child != nullptr; child = lxb_dom_node_next(child)) {
		lxb_tag_id_t tag = lxb_dom_node_tag_id(child);
		if (tag == LXB_TAG_TH || tag == LXB_TAG_TD) {
			cells.push_back(child);
		} else if (tag == LXB_TAG_TABLE) {
			continue;
		} else {
			collect_cells(child, cells);
		}
	}
}

void process_table(lxb_html_document_t *document, lxb_dom_node_t *table_node, std::string &output)
{
	std::string caption_text;
	for (lxb_dom_node_t *child = lxb_dom_node_first_child(table_node); child != nullptr; child = lxb_dom_node_next(child)) {
		if (lxb_dom_node_tag_id(child) == LXB_TAG_CAPTION) {
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

void walk_node(lxb_html_document_t *document, lxb_dom_node_t *node, std::string &out, bool rich, int &list_depth,
	       std::vector<int> &list_counters)
{
	if (!node)
		return;

	lxb_dom_node_type_t type = lxb_dom_node_type(node);
	if (type == LXB_DOM_NODE_TYPE_TEXT) {
		size_t len = 0;
		lxb_char_t *text = lxb_dom_node_text_content(node, &len);
		if (text) {
			std::string raw(reinterpret_cast<char *>(text), len);
			lxb_dom_document_destroy_text(lxb_dom_interface_document(document), text);
			out += collapse_whitespace(raw);
		}
		return;
	}

	lxb_tag_id_t tag = lxb_dom_node_tag_id(node);

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

	if (heading_level > 0) {
		append_paragraph_separator(out);
		out += std::string(heading_level, '#') + " ";
		for (lxb_dom_node_t *child = lxb_dom_node_first_child(node); child != nullptr; child = lxb_dom_node_next(child)) {
			walk_node(document, child, out, rich, list_depth, list_counters);
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
			walk_node(document, child, out, rich, list_depth, list_counters);
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
			walk_node(document, child, out, rich, list_depth, list_counters);
		}
		append_newline(out);
		return;
	}

	// 5. Paragraphs & Block Containers
	if (tag == LXB_TAG_P) {
		append_paragraph_separator(out);
		for (lxb_dom_node_t *child = lxb_dom_node_first_child(node); child != nullptr; child = lxb_dom_node_next(child)) {
			walk_node(document, child, out, rich, list_depth, list_counters);
		}
		append_paragraph_separator(out);
		return;
	}

	if (tag == LXB_TAG_DIV || tag == LXB_TAG_SECTION || tag == LXB_TAG_ARTICLE || tag == LXB_TAG_HEADER || tag == LXB_TAG_FOOTER) {
		append_newline(out);
		for (lxb_dom_node_t *child = lxb_dom_node_first_child(node); child != nullptr; child = lxb_dom_node_next(child)) {
			walk_node(document, child, out, rich, list_depth, list_counters);
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
			out += "[";
			for (lxb_dom_node_t *child = lxb_dom_node_first_child(node); child != nullptr; child = lxb_dom_node_next(child)) {
				walk_node(document, child, out, rich, list_depth, list_counters);
			}
			out += "](" + std::string(reinterpret_cast<const char *>(href_val), href_len) + ")";
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
		walk_node(document, child, out, rich, list_depth, list_counters);
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

	std::string extracted;
	int list_depth = 0;
	std::vector<int> list_counters;
	walk_node(document, body_node, extracted, args_.rich, list_depth, list_counters);

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

	return markdown_utils::align_all_tables(trim(final_out), false);
}

} // namespace tools
