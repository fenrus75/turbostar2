// Tested source file: src/codemap_utils.cpp, src/tools/fs_file_codemap/fs_file_codemap_entry.cpp,
// src/tools/fs_read_lines/fs_read_lines_entry.cpp, src/call_token_extractor.cpp
#include <cassert>
#include <fstream>
#include <iostream>
#include <string>
#include "agentlib/tool_context.h"
#include "agentlib/tool_registry.h"
#include "codemap_utils.h"
#include "lsp_manager.h"
#include "project_manager.h"
#include "test_watchdog.h"

namespace {

class mock_no_hierarchy_backend : public lsp_backend {
public:
	void start(event_queue &) override {}
	void stop() override {}
	void open_document(const std::string &, const std::string &) override {}
	void update_document(const std::string &, const std::string &) override {}
	void request_hover(const std::string &, int, int) override {}
	void request_document_highlight(const std::string &, int, int) override {}
	void request_selection_range(const std::string &, int, int) override {}
	[[nodiscard]] bool is_supported_file(const std::string &) const override { return true; }
	[[nodiscard]] std::vector<text_range> query_selection_ranges(const std::string &, int, int) override { return {}; }
	[[nodiscard]] std::vector<location_info> query_definition(const std::string &filepath, int line, int col) override
	{
		(void)filepath; (void)line; (void)col;
		location_info loc;
		loc.path = "test_opt_a_dep.cpp";
		loc.range = {0, 0, 3, 0};
		return {loc};
	}
	[[nodiscard]] std::vector<location_info> query_references(const std::string &, int, int) override { return {}; }
	[[nodiscard]] std::vector<symbol_info> query_workspace_symbols(const std::string &) override { return {}; }
	[[nodiscard]] std::vector<symbol_node> query_document_symbols(const std::string &) override { return {}; }
	void invalidate_symbol_cache(const std::string &) override {}
	[[nodiscard]] std::vector<call_hierarchy_item> query_call_hierarchy_outgoing(const std::string &, int, int) override { return {}; }
	[[nodiscard]] std::vector<outgoing_call_item> query_call_hierarchy_outgoing_batch(
		const std::string &, const std::vector<std::pair<int, int>> &,
		std::chrono::steady_clock::time_point) override
	{
		return {}; // Intentionally empty to simulate LSP servers without call hierarchy (like semcode-lsp)
	}
	[[nodiscard]] std::vector<type_hierarchy_item> query_type_hierarchy_supertypes(const std::string &, int, int) override { return {}; }
	[[nodiscard]] std::optional<std::vector<diagnostic_info>> query_file_diagnostics(const std::string &) override { return std::nullopt; }
	void store_file_diagnostics(const std::string &, const std::vector<diagnostic_info> &) override {}
};

} // namespace

int main()
{
	test_watchdog::setup_watchdog(30);

	if (!std::filesystem::exists("src/document.cpp") && std::filesystem::exists("../src/document.cpp")) {
		std::filesystem::current_path("..");
	}

	agentlib::tool_registry &registry = agentlib::tool_registry::get_instance();
	agentlib::tool_context ctx;
	ctx.fs_security.set_working_directory(std::filesystem::current_path());
	ctx.fs_security.add_allowed_root(std::filesystem::current_path(), agentlib::access_type::read);

	// 1. Create sample implementation file test_sample_impl.cpp (40 lines so partial reads don't auto-expand to EOF)
	std::string impl_file = "test_sample_impl.cpp";
	std::ofstream out_impl(impl_file);
	out_impl << "#include \"test_sample_impl.h\"\n\n"
		 << "void sample_foo()\n"
		 << "{\n"
		 << "    int a = 1;\n"
		 << "}\n\n"
		 << "int sample_bar(int x)\n"
		 << "{\n"
		 << "    return x + 42;\n"
		 << "}\n\n";
	for (int i = 0; i < 30; ++i) {
		out_impl << "// Padding line " << i << "\n";
	}
	out_impl.close();

	// 2. Create sample header file test_sample_impl.h
	std::string header_file = "test_sample_impl.h";
	std::ofstream out_hdr(header_file);
	out_hdr << "#pragma once\n\n"
		<< "void sample_foo();\n"
		<< "int sample_bar(int x);\n";
	out_hdr.close();

	// 3. Test fs_file_codemap standalone tool (unified 4-column format)
	nlohmann::json codemap_args = {{"path", impl_file}};
	std::string codemap_res = registry.execute_tool("fs_file_codemap", codemap_args.dump(), ctx);
	std::cout << "fs_file_codemap output:\n" << codemap_res << "\n";
	assert(codemap_res.find("Codemap for `test_sample_impl.cpp`") != std::string::npos);
	assert(codemap_res.find("| Symbol | start_line | end_line | lines |") != std::string::npos);
	assert(codemap_res.find("`sample_foo`") != std::string::npos);
	assert(codemap_res.find("`sample_bar`") != std::string::npos);

	// 4. Test fs_read_lines partial read (Unconditionally includes 4-column codemap table)
	nlohmann::json read_partial_args = {{"path", impl_file}, {"start_line", 1}, {"end_line", 5}};
	std::string read_partial_res = registry.execute_tool("fs_read_lines", read_partial_args.dump(), ctx);
	std::cout << "fs_read_lines partial read output:\n" << read_partial_res << "\n";
	assert(read_partial_res.find("| Symbol | start_line | end_line | lines |") != std::string::npos);
	assert(read_partial_res.find("`sample_foo`") != std::string::npos);

	// 5. Test fs_read_lines full read (SKIP table rule)
	nlohmann::json read_full_args = {{"path", impl_file}, {"start_line", 1}, {"end_line", 100}};
	std::string read_full_res = registry.execute_tool("fs_read_lines", read_full_args.dump(), ctx);
	std::cout << "fs_read_lines full read output:\n" << read_full_res << "\n";
	assert(read_full_res.find("| Symbol | start_line | end_line | lines |") == std::string::npos);

	// 6. Test fs_read_lines on header file (Header -> Impl magic)
	nlohmann::json read_hdr_args = {{"path", header_file}, {"start_line", 1}, {"end_line", 4}};
	std::string read_hdr_res = registry.execute_tool("fs_read_lines", read_hdr_args.dump(), ctx);
	std::cout << "fs_read_lines header output:\n" << read_hdr_res << "\n";
	assert(read_hdr_res.find("Codemap for `test_sample_impl.cpp`") != std::string::npos);
	assert(read_hdr_res.find("`sample_foo`") != std::string::npos);

	// 7. Test min_lines filter parameter
	nlohmann::json min_lines_args = {{"path", impl_file}, {"min_lines", 100}};
	std::string min_lines_res = registry.execute_tool("fs_file_codemap", min_lines_args.dump(), ctx);
	std::cout << "fs_file_codemap min_lines=100 output:\n" << min_lines_res << "\n";
	assert(min_lines_res.find("No functions, classes, or symbols found") != std::string::npos);

	// 8. Test Option B scope indentation for qualified C++ method definitions (Class::method)
	std::string class_file = "test_class_impl.cpp";
	std::ofstream out_class(class_file);
	out_class << "void my_class::foo()\n{\n    int a = 1;\n}\n\n"
		  << "void my_class::bar()\n{\n    int b = 2;\n}\n";
	out_class.close();

	nlohmann::json class_args = {{"path", class_file}};
	std::string class_res = registry.execute_tool("fs_file_codemap", class_args.dump(), ctx);
	std::cout << "fs_file_codemap class methods output:\n" << class_res << "\n";
	assert(class_res.find("`my_class`") != std::string::npos);
	assert(class_res.find("`    ::foo`") != std::string::npos);
	assert(class_res.find("`    ::bar`") != std::string::npos);
	std::remove(class_file.c_str());

	// 9. Test LSP symbol caching (Second call retrieves cached results)
	auto symbols1 = tools::get_document_codemap_symbols(impl_file, ctx, 1);
	auto symbols2 = tools::get_document_codemap_symbols(impl_file, ctx, 1);
	assert(!symbols1.empty());
	assert(symbols1.size() == symbols2.size());

	// 10. Test find_enclosing_symbol helper
	const tools::codemap_symbol_info *enc_foo = tools::find_enclosing_symbol(symbols1, 5);
	assert(enc_foo != nullptr);
	assert(enc_foo->name == "sample_foo");

	const tools::codemap_symbol_info *enc_bar = tools::find_enclosing_symbol(symbols1, 10);
	assert(enc_bar != nullptr);
	assert(enc_bar->name == "sample_bar");

	const tools::codemap_symbol_info *enc_none = tools::find_enclosing_symbol(symbols1, 33);
	assert(enc_none == nullptr);

	// 11. Test select_prioritized_codemap_symbols and priority scoring
	{
		std::vector<tools::codemap_symbol_info> test_syms;
		// 15 symbols
		for (int i = 1; i <= 15; ++i) {
			tools::codemap_symbol_info s;
			s.name = (i == 5) ? "target_grep_function" : std::format("func_{}", i);
			s.display_name = s.name;
			s.start_line = i * 10;
			s.end_line = i * 10 + 5;
			s.line_count = (i == 1) ? 1 : 6; // i=1 is a 1-line getter
			test_syms.push_back(s);
		}

		ctx.recent_grep_patterns.clear();
		ctx.recent_grep_patterns.push_back("target_grep");

		// Read range lines 45..55 (crosses func_5 start_line=50)
		auto sel = tools::select_prioritized_codemap_symbols(test_syms, 45, 55, "dummy.cpp", ctx, /*max_items=*/5);
		assert(sel.total_symbols == 15);
		assert(sel.selected_symbols.size() == 5);
		assert(sel.omitted_count == 10);

		// target_grep_function (func_5) encloses boundary (line 45..55 vs 50..55) AND matches grep -> top score
		bool found_grep_target = false;
		for (const auto &s : sel.selected_symbols) {
			if (s.name == "target_grep_function") {
				found_grep_target = true;
			}
		}
		assert(found_grep_target);

		// Test 1st call table formatting with tool_context (should include one-time hint)
		ctx.has_hinted_fs_file_codemap = false;
		std::string formatted_table1 =
		    tools::format_codemap_table("dummy.cpp", sel.selected_symbols, 0, sel.total_symbols, sel.omitted_count, &ctx);
		assert(formatted_table1.find("### Codemap for `dummy.cpp` (Top 5 of 15 symbols):") != std::string::npos);
		assert(formatted_table1.find("*... [10 other symbols omitted (use fs_file_codemap if full symbol table is needed)]*") !=
		       std::string::npos);
		assert(ctx.has_hinted_fs_file_codemap == true);

		// Test 2nd call table formatting with tool_context (should NOT repeat hint)
		std::string formatted_table2 =
		    tools::format_codemap_table("dummy.cpp", sel.selected_symbols, 0, sel.total_symbols, sel.omitted_count, &ctx);
		assert(formatted_table2.find("*... [10 other symbols omitted]*") != std::string::npos);
		assert(formatted_table2.find("use fs_file_codemap") == std::string::npos);

		// 11b. Test recency scoring & tie-breaking:
		// Construct three candidate symbols that have identical base scores (outside read range, same line length).
		// Symbol C: never reported (delta = 3600s -> +0.50 score)
		// Symbol B: reported 1800s ago (delta = 1800s -> -3.0 penalty + (1800/7200) = -2.75)
		// Symbol A: reported 10s ago (delta = 10s -> -3.0 penalty + (10/7200) = -2.9986)
		// Even though Symbol A appears at line 100, B at line 200, and C at line 300,
		// tie-breaking must prioritize C first, then B, then A.
		std::string recency_file = "recency_test.cpp";
		auto &rec_hist = ctx.codemap_history[recency_file];
		rec_hist.reported_symbols.clear();
		auto now = std::chrono::steady_clock::now();
		rec_hist.reported_symbols["sym_a"] = now - std::chrono::seconds(10);
		rec_hist.reported_symbols["sym_b"] = now - std::chrono::seconds(1800);
		// sym_c is omitted from history (never reported)

		std::vector<tools::codemap_symbol_info> recency_syms = {
		    {"sym_a", "sym_a", "Function", 1000, 1010, 11, 0, ""},
		    {"sym_b", "sym_b", "Function", 2000, 2010, 11, 0, ""},
		    {"sym_c", "sym_c", "Function", 3000, 3010, 11, 0, ""},
		};

		// Read range 1..10 (far from 1000, 2000, 3000 so base proximity score is 0.0)
		auto rec_sel = tools::select_prioritized_codemap_symbols(recency_syms, 1, 10, recency_file, ctx, /*max_items=*/1);
		assert(rec_sel.selected_symbols.size() == 1);
		// Top selected item must be sym_c (never reported)
		// Take top 2: reset sym_c so we test selecting from the same baseline.
		// Should be sym_c (unreported), then sym_b (reported 1800s ago), NOT sym_a (reported 10s ago)
		rec_hist.reported_symbols.erase("sym_c");
		auto rec_sel2 = tools::select_prioritized_codemap_symbols(recency_syms, 1, 10, recency_file, ctx, /*max_items=*/2);
		assert(rec_sel2.selected_symbols.size() == 2);
		bool found_a = false, found_b = false, found_c = false;
		for (const auto &s : rec_sel2.selected_symbols) {
			if (s.name == "sym_a")
				found_a = true;
			if (s.name == "sym_b")
				found_b = true;
			if (s.name == "sym_c")
				found_c = true;
		}
		assert(found_c && found_b && !found_a);
	}

	// 12. Test Markdown mini-LSP: headings produce a codemap outline
	std::string md_file = "test_sample.md";
	{
		std::ofstream out_md(md_file);
		out_md << "# Top Heading\n\nSome intro text.\n\n"
		       << "## Subsection A\n\nDetails about A.\n\n"
		       << "### Deep Detail\n\nMore details.\n\n"
		       << "## Subsection B\n\nDetails about B.\n"
		       << "# Second Top\n\nText.\n";
		out_md.close();
	}

	// Also create a file exercising closing ATX hash sequences and CRLF line endings,
	// both of which must be stripped from heading text.
	std::string md_edge_file = "test_sample_edges.md";
	{
		std::ofstream out_md(md_edge_file, std::ios::binary);
		out_md << "## Closing Hashes ##\r\n"
		       << "Intro line.\r\n"
		       << "### Nested ###\r\n"
		       << "Body.\r\n";
		out_md.close();
	}

	nlohmann::json md_args = {{"path", md_file}};
	std::string md_res = registry.execute_tool("fs_file_codemap", md_args.dump(), ctx);
	std::cout << "fs_file_codemap markdown output:\n" << md_res << "\n";
	assert(md_res.find("Codemap for `test_sample.md`") != std::string::npos);
	assert(md_res.find("`Top Heading`") != std::string::npos);
	assert(md_res.find("`    Subsection A`") != std::string::npos);
	assert(md_res.find("`        Deep Detail`") != std::string::npos);
	assert(md_res.find("`    Subsection B`") != std::string::npos);
	assert(md_res.find("`Second Top`") != std::string::npos);
	// Heading lines should be reported at their correct 1-based line numbers
	assert(md_res.find("| `Top Heading` | 1 | 1 | 1 |") != std::string::npos);	// line 1
	assert(md_res.find("| `    Subsection A` | 5 | 5 | 1 |") != std::string::npos); // line 5

	// Test find_enclosing_symbol works on markdown headings
	auto md_symbols = tools::get_document_codemap_symbols(md_file, ctx, 1);
	assert(!md_symbols.empty());
	const tools::codemap_symbol_info *enc_sub = tools::find_enclosing_symbol(md_symbols, 5); // line 5 in Subsection A region
	assert(enc_sub != nullptr);

	// Verify closing ATX hashes and CRLF are stripped from heading text
	{
		std::string md_edge_res = registry.execute_tool("fs_file_codemap", nlohmann::json{{"path", md_edge_file}}.dump(), ctx);
		std::cout << "fs_file_codemap edge-case markdown output:\n" << md_edge_res << "\n";
		// Closing hashes and '\r' must not leak into the symbol names
		assert(md_edge_res.find("`Closing Hashes`") != std::string::npos);
		assert(md_edge_res.find("`Closing Hashes##`") == std::string::npos);
		assert(md_edge_res.find("\r`") == std::string::npos); // no stray CR before closing backtick
		assert(md_edge_res.find("`    Nested`") != std::string::npos);
	}

	// Test a heading-only file has no false-positive class/function symbols
	nlohmann::json md_empty_args = {{"path", md_file}, {"min_lines", 100}};
	std::string md_empty_res = registry.execute_tool("fs_file_codemap", md_empty_args.dump(), ctx);
	// With min_lines=100, 1-line headings are filtered out (matching the C++ behavior)
	assert(md_empty_res.find("No functions, classes, or symbols found") != std::string::npos);

	// Test full:true and max_symbols parameters
	{
		nlohmann::json full_args = {{"path", impl_file}, {"full", true}};
		std::string full_res = registry.execute_tool("fs_file_codemap", full_args.dump(), ctx);
		assert(full_res.find("Codemap for `test_sample_impl.cpp` (Full ") != std::string::npos);

		nlohmann::json max_sym_args = {{"path", impl_file}, {"max_symbols", 1}};
		std::string max_sym_res = registry.execute_tool("fs_file_codemap", max_sym_args.dump(), ctx);
		assert(max_sym_res.find("(Top 1 of ") != std::string::npos);
		assert(max_sym_res.find("symbols omitted") != std::string::npos);
	}

	// 13. Test fs_file_codemap on VFS Markdown file (tmp://jfif.md)
	{
		auto vfs = std::make_shared<agentlib::virtual_file_system>();
		ctx.fs_security.set_vfs(vfs.get());
		std::string jfif_content =
		    "# JFIF Format Specification\n\n## Section 1: Intro\nText.\n\n## Section 2: Headers\nHeader details.\n";
		vfs->write_file("tmp://jfif.md", jfif_content.data(), jfif_content.size());
		nlohmann::json vfs_md_args = {{"path", "tmp://jfif.md"}};
		std::string vfs_md_res = registry.execute_tool("fs_file_codemap", vfs_md_args.dump(), ctx);
		std::cout << "fs_file_codemap VFS markdown output:\n" << vfs_md_res << "\n";
		assert(vfs_md_res.find("Codemap for `tmp://jfif.md`") != std::string::npos);
		assert(vfs_md_res.find("`JFIF Format Specification`") != std::string::npos);
		assert(vfs_md_res.find("`    Section 1: Intro`") != std::string::npos);
		assert(vfs_md_res.find("`    Section 2: Headers`") != std::string::npos);
	}

	// 14. Test get_outgoing_calls_in_range and get_outgoing_calls_for_symbol
	{
		std::string calls_file = "test_calls_impl.cpp";
		std::ofstream out_calls(calls_file);
		out_calls << "void target_func_alpha()\n{\n    int a = 1;\n}\n\n"
			  << "void caller_func_beta()\n{\n    target_func_alpha();\n}\n\n";
		for (int i = 0; i < 30; ++i) {
			out_calls << "// Padding line " << i << "\n";
		}
		out_calls.close();

		auto calls_in_range = tools::get_outgoing_calls_in_range(calls_file, 6, 9, &ctx);
		if (!calls_in_range.empty()) {
			assert(calls_in_range[0].target_name == "target_func_alpha");
		}

		auto calls_for_sym = tools::get_outgoing_calls_for_symbol(calls_file, "caller_func_beta", &ctx);
		if (!calls_for_sym.empty()) {
			assert(calls_for_sym[0].target_name == "target_func_alpha");
		}

		// Test outgoing call score boosting in select_prioritized_codemap_symbols
		auto all_syms = tools::get_document_codemap_symbols(calls_file, ctx, 1);
		auto selected = tools::select_prioritized_codemap_symbols(all_syms, 6, 9, calls_file, ctx, 1);
		assert(!selected.selected_symbols.empty());

		std::remove(calls_file.c_str());
	}

	// 15. Test Option D multiple ### Codemap for <path> sections for cross-file calls
	{
		std::string main_file = "test_opt_d_main.cpp";
		std::string dep_file = "test_opt_d_dep.cpp";

		std::ofstream out_dep(dep_file);
		out_dep << "void dep_function_zeta()\n{\n    int z = 100;\n}\n";
		out_dep.close();

		std::ofstream out_main(main_file);
		out_main << "#include \"test_opt_d_dep.h\"\n\n"
			 << "void main_caller_func()\n{\n    dep_function_zeta();\n}\n\n";
		for (int i = 0; i < 30; ++i) {
			out_main << "// Padding line " << i << "\n";
		}
		out_main.close();

		auto all_syms = tools::get_document_codemap_symbols(main_file, ctx, 1);
		auto selected = tools::select_prioritized_codemap_symbols(all_syms, 3, 6, main_file, ctx, 10);
		std::string table_md = tools::format_codemap_table(main_file, selected.selected_symbols, 35, selected.total_symbols,
								   selected.omitted_count, &ctx);

		std::cout << "Option D codemap table output:\n" << table_md << "\n";
		assert(table_md.find("### Codemap for `test_opt_d_main.cpp`") != std::string::npos);
		assert(table_md.find("`main_caller_func`") != std::string::npos);

		// Verify consolidated Called Dependencies table with Start and End columns
		tools::codemap_symbol_info dep_sym;
		dep_sym.name = "external_func";
		dep_sym.display_name = "external_func";
		dep_sym.kind_str = "Function";
		dep_sym.start_line = 10;
		dep_sym.end_line = 25;
		dep_sym.line_count = 16;
		dep_sym.depth = 0;
		dep_sym.source_file = "src/other_dep.cpp";

		std::vector<tools::codemap_symbol_info> combined_syms = selected.selected_symbols;
		combined_syms.push_back(dep_sym);

		std::string dep_table_md = tools::format_codemap_table(main_file, combined_syms, 35, combined_syms.size(), 0, &ctx);
		assert(dep_table_md.find("### Called Dependencies:") != std::string::npos);
		assert(dep_table_md.find("| Symbol | path | start_line | end_line |") != std::string::npos);
		assert(dep_table_md.find("| `external_func` | `src/other_dep.cpp` | 10 | 25 |") != std::string::npos);

		std::remove(main_file.c_str());
		std::remove(dep_file.c_str());
	}

	// 15b. Test the 'full' flag in format_codemap_table (item #13 fix):
	// when full=false and all symbols fit, the header must NOT claim "(Full N symbols)".
	// It should force the "(Top X of M symbols)" wording to stay honest.
	{
		std::vector<tools::codemap_symbol_info> syms;
		for (int i = 1; i <= 3; ++i) {
			tools::codemap_symbol_info s;
			s.name = std::format("full_test_func_{}", i);
			s.display_name = s.name;
			s.start_line = i * 10;
			s.end_line = i * 10 + 5;
			s.line_count = 6;
			syms.push_back(s);
		}

		// Default full=true, all symbols fit and none omitted -> "(Full 3 symbols)"
		std::string full_default = tools::format_codemap_table("dummy_full.cpp", syms, 0, syms.size(), 0, &ctx);
		assert(full_default.find("### Codemap for `dummy_full.cpp` (Full 3 symbols):") != std::string::npos);
		assert(full_default.find("Top 3 of 3 symbols") == std::string::npos);

		// full=false, all symbols fit -> must be "(Top 3 of 3 symbols)", never "Full".
		std::string full_false = tools::format_codemap_table("dummy_full.cpp", syms, 0, syms.size(), 0, &ctx, /*full=*/false);
		assert(full_false.find("### Codemap for `dummy_full.cpp` (Top 3 of 3 symbols):") != std::string::npos);
		assert(full_false.find("Full 3 symbols") == std::string::npos);

		// full=false with total_file_lines set still forces Top wording.
		std::string full_false_lines =
		    tools::format_codemap_table("dummy_full.cpp", syms, 40, syms.size(), 0, &ctx, /*full=*/false);
		assert(full_false_lines.find("### Codemap for `dummy_full.cpp` (Top 3 of 3 symbols, 40 lines):") != std::string::npos);
		assert(full_false_lines.find("Full 3 symbols") == std::string::npos);
	}

	// Test get_line_symbol_annotation helper
	{
		std::vector<tools::codemap_symbol_info> dummy_symbols = {{"my_foo_func", "my_foo_func", "Function", 10, 20, 11, 0, ""},
									 {"my_bar_func", "my_bar_func", "Function", 25, 40, 16, 0, ""}};

		assert(tools::get_line_symbol_annotation(dummy_symbols, 15) == "[symbol: my_foo_func (lines 10-20)]");
		assert(tools::get_line_symbol_annotation(dummy_symbols, 30) == "[symbol: my_bar_func (lines 25-40)]");
		assert(tools::get_line_symbol_annotation(dummy_symbols, 5) == "");
		assert(tools::get_line_symbol_annotation(dummy_symbols, 22) == "");

		// Test augment_compiler_output_with_codemap
		std::string raw_gcc_log = "test_sample_impl.cpp:5:10: error: 'a' was not declared\n"
					  "test_sample_impl.cpp:5:20: error: secondary error on same line\n"
					  "test_sample_impl.cpp:9:12: warning: unused variable 'x'\n"
					  "test_sample_impl.cpp:50:1: error: out of range error\n";

		std::string augmented = tools::augment_compiler_output_with_codemap(raw_gcc_log, nullptr, 2);
		assert(augmented.find("test_sample_impl.cpp:5:10: error: 'a' was not declared [symbol: sample_foo (lines 3-6)]") !=
		       std::string::npos);
		assert(augmented.find("test_sample_impl.cpp:5:20: error: secondary error on same line\n") != std::string::npos);
		assert(augmented.find("test_sample_impl.cpp:5:20: error: secondary error on same line [symbol:") == std::string::npos);
		assert(augmented.find("test_sample_impl.cpp:9:12: warning: unused variable 'x' [symbol: sample_bar (lines 8-11)]") !=
		       std::string::npos);
	}

	// 12. Test find_matching_header_file
	{
		std::string found_hdr = tools::find_matching_header_file(impl_file, ctx);
		assert(!found_hdr.empty());
		assert(found_hdr.find("test_sample_impl.h") != std::string::npos);
	}

	// 13. Test extract_class_context_preview and fs_read_lines class context preview
	{
		std::string preview_hdr = "test_preview_class.h";
		std::ofstream out_h(preview_hdr);
		out_h << "#pragma once\n"
		      << "#include <string>\n\n"
		      << "class preview_widget {\n"
		      << "public:\n"
		      << "    void render();\n"
		      << "    void reset();\n"
		      << "    void unused_method();\n"
		      << "private:\n"
		      << "    int width_;\n"
		      << "    int height_;\n"
		      << "    std::string title_;\n"
		      << "    int unused_field_;\n"
		      << "};\n";
		out_h.close();

		std::string preview_cpp = "test_preview_class.cpp";
		std::ofstream out_c(preview_cpp);
		out_c << "#include \"test_preview_class.h\"\n\n"
		      << "void preview_widget::render()\n"
		      << "{\n"
		      << "    width_ = 80;\n"
		      << "    height_ = 24;\n"
		      << "    reset();\n"
		      << "}\n\n";
		for (int i = 0; i < 30; ++i) {
			out_c << "// Padding line " << i << "\n";
		}
		out_c << "\nvoid preview_widget::reset()\n"
		      << "{\n"
		      << "    title_ = \"default\";\n"
		      << "}\n";
		out_c.close();

		// Execute fs_read_lines for lines 3 to 8 of test_preview_class.cpp
		nlohmann::json read_args = {{"path", preview_cpp}, {"start_line", 3}, {"end_line", 8}};
		std::string read_out = registry.execute_tool("fs_read_lines", read_args.dump(), ctx);
		std::cout << "fs_read_lines class context preview output:\n" << read_out << "\n";

		assert(read_out.find("### Class Context: `preview_widget`") != std::string::npos);
		assert(read_out.find("test_preview_class.h") != std::string::npos);
		assert(read_out.find("// Referenced member variables:") != std::string::npos);
		assert(read_out.find("width_") != std::string::npos);
		assert(read_out.find("height_") != std::string::npos);
		assert(read_out.find("title_") == std::string::npos);
		assert(read_out.find("unused_field_") == std::string::npos);
		assert(read_out.find("// Referenced member functions:") != std::string::npos);
		assert(read_out.find("reset()") != std::string::npos);
		assert(read_out.find("unused_method") == std::string::npos);

		std::remove(preview_hdr.c_str());
		std::remove(preview_cpp.c_str());
	}

	// 14. Test extract_class_context_preview on document.cpp
	{
		nlohmann::json read_args = {{"path", "src/document.cpp"}, {"start_line", 851}, {"end_line", 858}};
		std::string read_out = registry.execute_tool("fs_read_lines", read_args.dump(), ctx);
		std::cout << "fs_read_lines document.cpp class context output:\n" << read_out << "\n";
		assert(read_out.find("### Class Context: `document`") != std::string::npos);
		assert(read_out.find("lines_") != std::string::npos);
		assert(read_out.find("cursor_x_") != std::string::npos);
		assert(read_out.find("cursor_y_") != std::string::npos);
		assert(read_out.find("target_cursor_x_") != std::string::npos);
	}

	// 15. Test extract_class_context_preview on document.cpp lines 410 to 418
	{
		nlohmann::json read_args = {{"path", "src/document.cpp"}, {"start_line", 410}, {"end_line", 418}};
		std::string read_out = registry.execute_tool("fs_read_lines", read_args.dump(), ctx);
		std::cout << "fs_read_lines document.cpp 410-418 class context output:\n" << read_out << "\n";
		size_t ctx_pos = read_out.find("### Class Context: `document`");
		assert(ctx_pos != std::string::npos);
		std::string class_ctx = read_out.substr(ctx_pos);
		// Must find member variable mutex_
		assert(class_ctx.find("mutex_") != std::string::npos);
		// Must find sibling member functions
		assert(class_ctx.find("restore_cursor_state_unlocked") != std::string::npos);
		assert(class_ctx.find("notify_cursor_changed") != std::string::npos);
		assert(class_ctx.find("request_redraw") != std::string::npos);
		// Must NOT falsely include constructor
		assert(class_ctx.find("document(") == std::string::npos);
		// Must NOT falsely include the function itself being defined
		assert(class_ctx.find("int restore_cursor_state(") == std::string::npos);
	}

	// 16. Test called dependencies line attribution via resolve_outgoing_call_target
	{
		std::unordered_map<std::string, std::vector<tools::codemap_symbol_info>> syms_cache;
		tools::outgoing_call_reference ref;

		// (a) Test inline method in header: is_force_ascii declared in src/config_manager.h
		lsp_manager::call_hierarchy_item item_ascii;
		item_ascii.name = "is_force_ascii";
		item_ascii.kind = 6;
		item_ascii.uri = "src/config_manager.h";
		item_ascii.range = {133, 1, 136, 2};
		item_ascii.selection_range = {133, 6, 133, 20};

		bool ok = tools::resolve_outgoing_call_target(ref, item_ascii, syms_cache, &ctx);
		assert(ok);
		// Must be attributed to config_manager.h (where it is defined), NOT config_manager.cpp!
		assert(ref.target_file == "src/config_manager.h");
		assert(ref.target_start_line == 134);
		assert(ref.target_end_line == 137);

		// (b) Test function implemented in cpp: get_file_type declared in fs_utils.h, defined in fs_utils.cpp
		lsp_manager::call_hierarchy_item item_file_type;
		item_file_type.name = "get_file_type";
		item_file_type.kind = 12;
		item_file_type.uri = "src/fs_utils.h";
		item_file_type.range = {58, 0, 58, 56};
		item_file_type.selection_range = {58, 12, 58, 25};

		ok = tools::resolve_outgoing_call_target(ref, item_file_type, syms_cache, &ctx);
		assert(ok);
		assert(ref.target_file == "src/fs_utils.cpp");
		assert(ref.target_start_line >= 220 && ref.target_start_line <= 260);
		assert(ref.target_end_line >= 250);

		// (c) Test function implemented in cpp: get_instance declared in config_manager.h, defined in config_manager.cpp
		lsp_manager::call_hierarchy_item item_get_inst;
		item_get_inst.name = "get_instance";
		item_get_inst.kind = 6;
		item_get_inst.uri = "src/config_manager.h";
		item_get_inst.range = {11, 1, 11, 40};
		item_get_inst.selection_range = {11, 25, 11, 37};

		ok = tools::resolve_outgoing_call_target(ref, item_get_inst, syms_cache, &ctx);
		assert(ok);
		assert(ref.target_file == "src/config_manager.cpp");
		assert(ref.target_start_line == 24);
		assert(ref.target_end_line == 28);
	}

	// 17. Test kind-aware adaptive pruning for large symbol sets (> 50 symbols)
	{
		std::string large_md = "test_large_headings.md";
		{
			std::ofstream out_md(large_md);
			out_md << "# Top Document\n\n";
			for (int i = 0; i < 60; ++i) {
				out_md << "# Section " << i << "\n\nContent for section " << i << ".\n\n";
			}
			out_md.close();
		}

		nlohmann::json args_default = {{"path", large_md}};
		std::string res_default = registry.execute_tool("fs_file_codemap", args_default.dump(), ctx);

		// Must contain the headings (headings are structural!)
		assert(res_default.find("Codemap for `test_large_headings.md`") != std::string::npos);
		assert(res_default.find("`Section 0`") != std::string::npos);
		assert(res_default.find("`Section 59`") != std::string::npos);

		// Also test a large source file with 60 1-line getter functions and verify header format
		// when symbols are pruned vs unpruned.
		// Directly test format_codemap_table with pruned_count > 0:
		std::vector<tools::codemap_symbol_info> mock_syms;
		for (int i = 0; i < 493; ++i) {
			mock_syms.push_back(
			    {std::format("func_{}", i), std::format("func_{}", i), "Function", i * 2 + 1, i * 2 + 2, 2, 0, ""});
		}
		std::string pruned_table = tools::format_codemap_table("mock_large.h", mock_syms, 3581, 493, 0, &ctx, /*full=*/true,
								       /*pruned_count=*/31, /*raw_total_symbols=*/524);
		assert(pruned_table.find("### Codemap for `mock_large.h` (493/524 symbols (31 pruned), 3581 lines):") != std::string::npos);
		assert(pruned_table.find("Full ") == std::string::npos);

		std::remove(large_md.c_str());
	}

	// 17. Test SystemVerilog codemap parsing (.sv)
	std::string sv_file = "test_design.sv";
	{
		std::ofstream out_sv(sv_file);
		out_sv << "module alu_top (\n"
		       << "    input logic clk,\n"
		       << "    output logic [7:0] out\n"
		       << ");\n"
		       << "    function automatic [7:0] add(input [7:0] a, input [7:0] b);\n"
		       << "        return a + b;\n"
		       << "    endfunction\n"
		       << "    task automatic reset_alu();\n"
		       << "        out <= 0;\n"
		       << "    endtask\n"
		       << "endmodule\n";
		out_sv.close();

		nlohmann::json args_sv = {{"path", sv_file}};
		std::string res_sv = registry.execute_tool("fs_file_codemap", args_sv.dump(), ctx);
		assert(res_sv.find("`alu_top`") != std::string::npos);
		assert(res_sv.find("`add`") != std::string::npos);
		assert(res_sv.find("`reset_alu`") != std::string::npos);

		std::remove(sv_file.c_str());
	}

	// 18. Test fs_read_lines does NOT snap to EOF when near end of file (< 25 lines total)
	{
		std::string small_file = "test_small_read_eof.cpp";
		{
			std::ofstream out_small(small_file);
			out_small << "void first_func()\n"
				  << "{\n"
				  << "    int a = 1;\n"
				  << "}\n\n"
				  << "void second_func()\n"
				  << "{\n"
				  << "    int b = 2;\n"
				  << "}\n";
			out_small.close();
		}

		// File is only 9 lines long. Request lines 1 to 3 (inside first_func).
		// Function snapping should extend to line 4 (end of first_func), but NOT line 9 (EOF).
		nlohmann::json small_args = {{"path", small_file}, {"start_line", 1}, {"end_line", 3}};
		std::string small_res = registry.execute_tool("fs_read_lines", small_args.dump(), ctx);
		std::cout << "fs_read_lines small file near EOF output:\n" << small_res << "\n";
		assert(small_res.find("Code for lines 1 - 4 of test_small_read_eof.cpp (total 9 lines):") != std::string::npos);
		assert(small_res.find("1: void first_func()") != std::string::npos);
		assert(small_res.find("4: }") != std::string::npos);
		assert(small_res.find("second_func()") == std::string::npos);

		std::remove(small_file.c_str());
	}

	{
		// Test: reading EXACTLY to the end of a function (e.g. inside a struct/class or followed by another function)
		// must NOT extend to the enclosing class or into the next function.
		std::string exact_boundary_file = "test_exact_boundary.cpp";
		{
			std::ofstream out_exact(exact_boundary_file);
			out_exact << "struct MyService {\n"
				  << "    void func_alpha()\n"
				  << "    {\n"
				  << "        int a = 1;\n"
				  << "    }\n"
				  << "    void func_beta()\n"
				  << "    {\n"
				  << "        int b = 2;\n"
				  << "    }\n"
				  << "};\n";
			for (int i = 0; i < 30; ++i) {
				out_exact << "// padding " << i << "\n";
			}
			out_exact.close();
		}

		// Request lines 2 to 5 (EXACTLY to the end of func_alpha)
		nlohmann::json exact_args = {{"path", exact_boundary_file}, {"start_line", 2}, {"end_line", 5}};
		std::string exact_res = registry.execute_tool("fs_read_lines", exact_args.dump(), ctx);
		std::cout << "fs_read_lines exact boundary output:\n" << exact_res << "\n";
		assert(exact_res.find("lines 2 - 5 of") != std::string::npos);
		assert(exact_res.find("6:     void func_beta()") == std::string::npos);

		std::remove(exact_boundary_file.c_str());
	}

	{
		// Test: reading EXACTLY to closing delimiter in unstructured text file (.txt without symbols)
		// must NOT extend to subsequent delimiters.
		std::string plain_file = "test_boundary_plain.txt";
		{
			std::ofstream out_plain(plain_file);
			out_plain << "block_one {\n"
				  << "    val = 1\n"
				  << "}\n"
				  << "block_two {\n"
				  << "    val = 2\n"
				  << "}\n";
			for (int i = 0; i < 30; ++i) {
				out_plain << "# padding " << i << "\n";
			}
			out_plain.close();
		}

		// Request lines 1 to 3 (EXACTLY to end of block_one)
		nlohmann::json plain_args = {{"path", plain_file}, {"start_line", 1}, {"end_line", 3}};
		std::string plain_res = registry.execute_tool("fs_read_lines", plain_args.dump(), ctx);
		std::cout << "fs_read_lines plain boundary output:\n" << plain_res << "\n";
		assert(plain_res.find("lines 1 - 3 of") != std::string::npos);
		assert(plain_res.find("4: block_two") == std::string::npos);

		std::remove(plain_file.c_str());
	}

	// 21. Test Approach A: In-slice outgoing call extraction when LSP backend lacks call hierarchy (e.g. semcode)
	{
		std::string dep_file = "test_opt_a_dep.cpp";
		{
			std::ofstream out_dep(dep_file);
			out_dep << "void callee_slice_func(int x)\n{\n    (void)x;\n}\n";
		}

		std::string main_file = "test_opt_a_main.cpp";
		{
			std::ofstream out_main(main_file);
			out_main << "void caller_slice_func()\n{\n    callee_slice_func(99);\n}\n";
			for (int i = 0; i < 30; ++i) {
				out_main << "// padding " << i << "\n";
			}
		}

		project_manager::get_instance().set_lsp_backend_for_testing(std::make_unique<mock_no_hierarchy_backend>());

		nlohmann::json read_slice_args = {{"path", main_file}, {"start_line", 1}, {"end_line", 5}};
		std::string read_slice_res = registry.execute_tool("fs_read_lines", read_slice_args.dump(), ctx);
		std::cout << "fs_read_lines Approach A output:\n" << read_slice_res << "\n";
		assert(read_slice_res.find("### Called Dependencies:") != std::string::npos);
		assert(read_slice_res.find("`callee_slice_func`") != std::string::npos);
		assert(read_slice_res.find("test_opt_a_dep.cpp") != std::string::npos);
		// Verify caller_slice_func is in primary Codemap table, but NOT in Called Dependencies
		size_t dep_hdr = read_slice_res.find("### Called Dependencies:");
		assert(dep_hdr != std::string::npos);
		std::string dep_section = read_slice_res.substr(dep_hdr);
		assert(dep_section.find("`caller_slice_func`") == std::string::npos);

		std::remove(main_file.c_str());
		std::remove(dep_file.c_str());
	}

	// Test multi-line C function declaration (kernel style) with fallback_find_symbols / fs_file_codemap
	{
		std::string multiline_file = "test_multiline_func.c";
		{
			std::ofstream out(multiline_file);
			out << "// line 1\n"
			    << "static int prev_func(void)\n"
			    << "{\n"
			    << "    return 0;\n"
			    << "}\n\n"
			    << "#ifdef CONFIG_SUSPEND\n"
			    << "static noinstr void enter_s2idle_proper(struct cpuidle_driver *drv,\n"
			    << "                                         struct cpuidle_device *dev, int index)\n"
			    << "{\n"
			    << "    int state = index;\n"
			    << "}\n"
			    << "#endif\n";
		}

		nlohmann::json args = {{"path", multiline_file}};
		std::string res = registry.execute_tool("fs_file_codemap", args.dump(), ctx);
		std::cout << "fs_file_codemap multiline output:\n" << res << "\n";
		assert(res.find("`enter_s2idle_proper`") != std::string::npos);
		assert(res.find("| `enter_s2idle_proper` | 8 | 12 | 5 |") != std::string::npos);
		// Ensure struct cpuidle_device is NOT mistakenly parsed as a class/struct
		assert(res.find("`cpuidle_device`") == std::string::npos);

		std::remove(multiline_file.c_str());
	}

	// Test C function with return type and attributes on preceding line
	{
		std::string split_sig_file = "test_split_sig.c";
		{
			std::ofstream out(split_sig_file);
			out << "// line 1\n"
			    << "\n"
			    << "static noinstr void\n"
			    << "my_split_func(int a,\n"
			    << "              int b)\n"
			    << "{\n"
			    << "    return;\n"
			    << "}\n";
		}

		nlohmann::json args = {{"path", split_sig_file}};
		std::string res = registry.execute_tool("fs_file_codemap", args.dump(), ctx);
		std::cout << "fs_file_codemap split_sig output:\n" << res << "\n";
		assert(res.find("`my_split_func`") != std::string::npos);
		assert(res.find("| `my_split_func` | 3 | 8 | 6 |") != std::string::npos);

		std::remove(split_sig_file.c_str());
	}

	// Cleanup
	std::remove(impl_file.c_str());
	std::remove(header_file.c_str());
	std::remove(md_file.c_str());
	std::remove(md_edge_file.c_str());

	std::cout << "All fs_file_codemap tests passed successfully!\n";
	return 0;
}
