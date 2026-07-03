#include "plugins/html/html_extract_tables_tool.h"
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
	// Look for caption
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

	// Pad rows
	for (auto &row : grid) {
		while (row.size() < max_cols) {
			row.push_back("");
		}
	}

	if (!caption_text.empty()) {
		output += "**Table: " + caption_text + "**\n\n";
	}

	// Render Markdown table
	// Headers
	output += "|";
	for (size_t col = 0; col < max_cols; ++col) {
		output += " " + grid[0][col] + " |";
	}
	output += "\n|";
	for (size_t col = 0; col < max_cols; ++col) {
		output += " --- |";
	}
	output += "\n";

	// Data rows
	for (size_t r = 1; r < grid.size(); ++r) {
		output += "|";
		for (size_t col = 0; col < max_cols; ++col) {
			output += " " + grid[r][col] + " |";
		}
		output += "\n";
	}
	output += "\n";
}

void traverse_dom(lxb_html_document_t *document, lxb_dom_node_t *node, std::vector<std::string> &headings, std::vector<std::string> &last_emitted, std::string &output, int &tables_found)
{
	if (!node)
		return;

	lxb_tag_id_t tag = lxb_dom_node_tag_id(node);

	if (tag >= LXB_TAG_H1 && tag <= LXB_TAG_H3) {
		int level = tag - LXB_TAG_H1;
		size_t len = 0;
		lxb_char_t *text = lxb_dom_node_text_content(node, &len);
		if (text) {
			std::string header_str = trim(std::string(reinterpret_cast<char *>(text), len));
			headings[level] = header_str;
			for (int i = level + 1; i < 3; ++i) {
				headings[i].clear();
			}
			lxb_dom_document_destroy_text(lxb_dom_interface_document(document), text);
		}
	} else if (tag == LXB_TAG_TABLE) {
		// Emit headings
		bool changed = false;
		for (int i = 0; i < 3; ++i) {
			if (headings[i] != last_emitted[i] || changed) {
				if (!headings[i].empty()) {
					output += std::string(i + 1, '#') + " " + headings[i] + "\n";
					last_emitted[i] = headings[i];
				} else {
					last_emitted[i].clear();
				}
				changed = true;
			}
		}
		if (changed) {
			output += "\n";
		}

		process_table(document, node, output);
		tables_found++;
	}

	// Traverse children
	for (lxb_dom_node_t *child = lxb_dom_node_first_child(node); child != nullptr; child = lxb_dom_node_next(child)) {
		traverse_dom(document, child, headings, last_emitted, output, tables_found);
	}
}

} // namespace

html_extract_tables_tool::html_extract_tables_tool(html_extract_tables_args args)
    : llm_tool_action("Extracting tables from HTML file"), args_(std::move(args))
{
}

bool html_extract_tables_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string html_extract_tables_tool::execute(agentlib::tool_context & /*ctx*/)
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

	std::string raw_markdown;
	int tables_found = 0;
	std::vector<std::string> headings(3, "");
	std::vector<std::string> last_emitted_headings(3, "");

	traverse_dom(document, body_node, headings, last_emitted_headings, raw_markdown, tables_found);

	lxb_html_document_destroy(document);

	if (tables_found == 0) {
		return "No tables found in the HTML file.";
	}

	// Align all tables generated using markdown_utils helper
	std::string aligned_markdown = markdown_utils::align_all_tables(raw_markdown, false);

	if (!args_.safe_output_path.empty()) {
		std::ofstream ofs(args_.safe_output_path, std::ios::binary);
		if (!ofs) {
			return "Error: Unable to write to output path " + args_.output_path;
		}
		ofs << aligned_markdown;
		ofs.close();
		return "Successfully extracted " + std::to_string(tables_found) + " tables and wrote them to " + args_.output_path + ".";
	}

	return aligned_markdown;
}

} // namespace tools
