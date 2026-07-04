#include "test_watchdog.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "agentlib/tool_registry.h"
#include "filter_registry.h"
#include "pluginloader.h"
#include "project_manager.h"

using namespace agentlib;

int main()
{
	test_watchdog::setup_watchdog(30);
	project_manager::get_instance().initialize();

	tool_registry &registry = tool_registry::get_instance();

	// Load the dynamic plugins (including html if lexbor is found)
	auto &loader = plugin_loader::get_instance();
	loader.load_all_plugins();

	// If the html plugin wasn't loaded (e.g. lexbor not found during compilation), skip test
	if (!registry.has_tool_family("html")) {
		std::cout << "html plugin not registered (lexbor probably missing). Skipping test.\n";
		return 0;
	}

	tool_context ctx;
	ctx.properties.active_families = {"html"};

	std::string project_root = project_manager::get_instance().get_project_root();
	ctx.fs_security.set_working_directory(project_root);
	ctx.fs_security.add_allowed_root(project_root, access_type::read);
	ctx.fs_security.add_allowed_root(project_root, access_type::write);

	std::string html_path = "tests/unit/mock_table.html";
	std::string full_html_path = project_root + "/" + html_path;

	std::string large_html_path = "tests/unit/mock_large.html";
	std::string full_large_html_path = project_root + "/" + large_html_path;

	std::string out_path = "tests/unit/table_output.md";
	std::string full_out_path = project_root + "/" + out_path;

	// 1. Write structured mock HTML to disk
	std::string html_content = R"(
		<!DOCTYPE html>
		<html>
		<head><title>Mock HTML</title></head>
		<body>
			<h1>Document Title</h1>
			<p>Introductory paragraph with a <a href="https://example.com/info">link here</a>.</p>
			<img src="assets/banner.png" alt="Welcome banner" />
			<p>Here is some <b>bold text</b> and <i>italic text</i>, and inline <code>code snippet</code>.</p>
			<ul>
				<li>First list item</li>
				<li>Second list item</li>
			</ul>
			<h2>Section A</h2>
			<p>Details about Section A</p>
			<h3>Subsection A.1</h3>
			<table>
				<caption>Mock Data Table</caption>
				<thead>
					<tr><th>Name</th><th>Value</th><th>Desc</th></tr>
				</thead>
				<tbody>
					<tr><td>Item 1</td><td>100</td><td>First item</td></tr>
					<tr><td>Item 2</td><td>200</td><td>Second item | value</td></tr>
				</tbody>
			</table>
			<h4>Subsection A.1.1 (Deep)</h4>
			<table>
				<tr><td>Unstructured Cell 1</td><td>Unstructured Cell 2</td></tr>
			</table>
		</body>
		</html>
	)";

	std::ofstream ofs(full_html_path, std::ios::binary);
	if (!ofs.is_open()) {
		std::cerr << "Failed to open mock HTML file for writing: " << full_html_path << std::endl;
		return 1;
	}
	ofs << html_content;
	ofs.close();

	std::cout << "Testing html_extract_tables..." << std::endl;

	// 2. Test family activation constraint (inactive by default)
	ctx.is_family_active = [](const std::string &family) { return family != "html"; };

	{
		std::string args = "{\"path\": \"" + html_path + "\"}";
		auto prep = registry.prepare_tool("html_extract_tables", args, ctx);
		assert(prep.tool == nullptr && "html_extract_tables must block if html family is inactive");
		assert(prep.error_message.find("Security Violation") != std::string::npos);
	}

	// 3. Test family activation constraint (active)
	ctx.is_family_active = [](const std::string &family) { return family == "html"; };

	// 4. Test extraction to console (returning markdown)
	{
		std::string args = "{\"path\": \"" + html_path + "\"}";
		std::string res = registry.execute_tool("html_extract_tables", args, ctx);
		std::cout << "console extract result:\n" << res << "\n";
		assert(!res.empty());
		assert(res.find("Error:") == std::string::npos);
		assert(res.find("# Document Title") != std::string::npos);
		assert(res.find("## Section A") != std::string::npos);
		assert(res.find("### Subsection A.1") != std::string::npos);
		assert(res.find("Subsection A.1.1") == std::string::npos); // Should be cut off because we limit to h3
		assert(res.find("**Table: Mock Data Table**") != std::string::npos);
		assert(res.find("Item 2") != std::string::npos);
		assert(res.find("Second item") != std::string::npos); // Verify content is present
	}

	// 5. Test extraction to file
	{
		std::string args = "{\"path\": \"" + html_path + "\", \"output_path\": \"" + out_path + "\"}";
		std::string res = registry.execute_tool("html_extract_tables", args, ctx);
		std::cout << "file extract result: " << res << "\n";
		assert(res.find("Successfully extracted") != std::string::npos);
		assert(res.find(out_path) != std::string::npos);

		// Read output file and assert content
		std::ifstream ifs(full_out_path, std::ios::binary);
		assert(ifs.is_open());
		std::string out_content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
		ifs.close();

		assert(out_content.find("# Document Title") != std::string::npos);
		assert(out_content.find("Item 1") != std::string::npos);
	}

	// 6. Test 5MB size limit validation
	{
		// Create a sparse file of 5.1MB
		std::ofstream ofs_large(full_large_html_path, std::ios::binary);
		if (ofs_large.is_open()) {
			ofs_large.seekp(5 * 1024 * 1024 + 1000);
			ofs_large.write("", 1);
			ofs_large.close();

			std::string args = "{\"path\": \"" + large_html_path + "\"}";
			auto prep = registry.prepare_tool("html_extract_tables", args, ctx);
			assert(prep.tool == nullptr && "validator must reject files over 5MB");
			assert(prep.error_message.find("exceeds the 5MB size limit") != std::string::npos);
		}
	}

	// 7. Test html_list_links
	{
		std::string args = "{\"path\": \"" + html_path + "\"}";
		std::string res = registry.execute_tool("html_list_links", args, ctx);
		std::cout << "list links result:\n" << res << "\n";
		assert(!res.empty());
		assert(res.find("Error:") == std::string::npos);
		assert(res.find("link here") != std::string::npos);
		assert(res.find("https://example.com/info") != std::string::npos);
	}

	// 8. Test html_list_images
	{
		std::string args = "{\"path\": \"" + html_path + "\"}";
		std::string res = registry.execute_tool("html_list_images", args, ctx);
		std::cout << "list images result:\n" << res << "\n";
		assert(!res.empty());
		assert(res.find("Error:") == std::string::npos);
		assert(res.find("Welcome banner") != std::string::npos);
		assert(res.find("assets/banner.png") != std::string::npos);
	}

	// 9. Test html_extract_text (rich = true, default)
	{
		std::string args = "{\"path\": \"" + html_path + "\"}";
		std::string res = registry.execute_tool("html_extract_text", args, ctx);
		std::cout << "extract text rich result:\n" << res << "\n";
		assert(!res.empty());
		assert(res.find("Error:") == std::string::npos);
		assert(res.find("# Document Title") != std::string::npos);
		assert(res.find("**bold text**") != std::string::npos);
		assert(res.find("*italic text*") != std::string::npos);
		assert(res.find("`code snippet`") != std::string::npos);
		assert(res.find("- First list item") != std::string::npos);
		assert(res.find("[link here](https://example.com/info)") != std::string::npos);
		assert(res.find("Item 2") != std::string::npos); // Table content preserved
	}

	// 10. Test html_extract_text (rich = false)
	{
		std::string args = "{\"path\": \"" + html_path + "\", \"rich\": false}";
		std::string res = registry.execute_tool("html_extract_text", args, ctx);
		std::cout << "extract text non-rich result:\n" << res << "\n";
		assert(!res.empty());
		assert(res.find("Error:") == std::string::npos);
		assert(res.find("bold text") != std::string::npos);
		assert(res.find("**bold text**") == std::string::npos); // bold tag stripped
		assert(res.find("italic text") != std::string::npos);
		assert(res.find("*italic text*") == std::string::npos); // italic tag stripped
		assert(res.find("`code snippet`") != std::string::npos); // inline code always kept
	}

	// 11. Test filter registry integration for html_to_markdown
	{
		std::string html_in = "<h1>Hello</h1><p>World with <b>bold</b>.</p>";
		bool success = false;
		std::string res = filter_registry::get_instance().apply_filter("html_to_markdown", html_in, success);
		assert(success);
		std::cout << "filter html_to_markdown result:\n" << res << "\n";
		assert(res.find("# Hello") != std::string::npos);
		assert(res.find("**bold**") != std::string::npos);
	}

	// 12. Test filter registry integration for html_to_markdown_plain
	{
		std::string html_in = "<h1>Hello</h1><p>World with <b>bold</b>.</p>";
		bool success = false;
		std::string res = filter_registry::get_instance().apply_filter("html_to_markdown_plain", html_in, success);
		assert(success);
		std::cout << "filter html_to_markdown_plain result:\n" << res << "\n";
		assert(res.find("# Hello") != std::string::npos);
		assert(res.find("bold") != std::string::npos);
		assert(res.find("**bold**") == std::string::npos);
	}

	// 13. Test filter registry integration for html_extract_tables
	{
		std::string html_in = "<table><tr><td>Cell A</td><td>Cell B</td></tr></table>";
		bool success = false;
		std::string res = filter_registry::get_instance().apply_filter("html_extract_tables", html_in, success);
		assert(success);
		std::cout << "filter html_extract_tables result:\n" << res << "\n";
		assert(res.find("| Cell A | Cell B |") != std::string::npos);
	}

	// Clean up mock files
	std::filesystem::remove(full_html_path);
	std::filesystem::remove(full_large_html_path);
	std::filesystem::remove(full_out_path);

	std::cout << "html plugin tool tests passed successfully.\n";
	return 0;
}
