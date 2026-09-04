#include "test_watchdog.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include "../../src/agentlib/document_provider.h"
#include "../../src/agentlib/tool_context.h"
#include "../../src/tools/fs_grep_files/fs_grep_files.h"

namespace fs = std::filesystem;

class mock_document_snapshot : public agentlib::document_snapshot
{
      public:
	std::vector<std::string> lines;

	size_t get_line_count() const override
	{
		return lines.size();
	}
	std::string get_line_text(size_t index) const override
	{
		return lines[index];
	}
	std::vector<agentlib::diagnostic_snapshot> get_diagnostics() const override
	{
		return {};
	}
};

class mock_document_provider : public agentlib::document_provider
{
      public:
	std::string mock_open_file;
	std::vector<std::string> mock_lines;

	std::vector<std::string> get_open_document_paths() const override
	{
		if (!mock_open_file.empty())
			return {mock_open_file};
		return {};
	}

	std::unique_ptr<agentlib::document_snapshot> get_open_document(std::string_view safe_path) const override
	{
		if (safe_path == mock_open_file) {
			auto snap = std::make_unique<mock_document_snapshot>();
			snap->lines = mock_lines;
			return snap;
		}
		return nullptr;
	}

	bool apply_live_edits(std::string_view /*safe_path*/, std::string_view /*edits_json_payload*/) override
	{
		return false;
	}

	void save_all_documents() override
	{
	}
};

class test_fs_grep_files_tool_mocked : public tools::fs_grep_files_tool
{
      public:
	using tools::fs_grep_files_tool::fs_grep_files_tool;
	std::vector<lsp_manager::symbol_info> mock_symbols;

      protected:
	std::vector<lsp_manager::symbol_info> get_lsp_symbols(const std::string &query) override
	{
		(void)query;
		return mock_symbols;
	}
};

int main()
{
	test_watchdog::setup_watchdog(30);
	fs::path temp_dir = fs::temp_directory_path() / "turbostar_test_grep";
	fs::create_directories(temp_dir);

	// Create a file on disk
	fs::path file1 = temp_dir / "test1.txt";
	std::ofstream out1(file1);
	out1 << "line 1\n";
	out1 << "hello world\n";
	out1 << "line 3\n";
	out1.close();

	agentlib::tool_context ctx;
	ctx.fs_security.set_working_directory(temp_dir);
	ctx.fs_security.add_allowed_root(temp_dir, agentlib::access_type::read);

	// Test 1: Disk search (mmap)
	tools::fs_grep_files_args args1;
	args1.pattern = "hello";
	args1.safe_search_path = temp_dir.string();
	tools::fs_grep_files_tool tool1(args1);

	std::string res1 = tool1.execute(ctx);
	assert(res1.find("Found 1 matches across 1 files") != std::string::npos);
	assert(res1.find("Line 2") != std::string::npos);

	// Test 2: Editor buffer search
	mock_document_provider doc_prov;
	doc_prov.mock_open_file = file1.string();
	doc_prov.mock_lines = {"line 1 modified", "hello dirty buffer", "line 3"};
	ctx.doc_provider = &doc_prov;

	tools::fs_grep_files_args args2;
	args2.pattern = "dirty";
	args2.safe_search_path = temp_dir.string();
	tools::fs_grep_files_tool tool2(args2);

	std::string res2 = tool2.execute(ctx);
	assert(res2.find("Found 1 matches across 1 files") != std::string::npos);
	assert(res2.find("dirty buffer") != std::string::npos); // Should match the mock buffer, not disk

	// Test 3: Context Lines
	tools::fs_grep_files_args args3;
	args3.pattern = "hello";
	args3.safe_search_path = temp_dir.string();
	args3.context_lines = 1;
	tools::fs_grep_files_tool tool3(args3);

	std::string res3 = tool3.execute(ctx);
	std::cout << "RES3:\n" << res3 << std::endl;
	assert(res3.find("**Match near Line 1**:") != std::string::npos);
	assert(res3.find("1: line 1 modified") != std::string::npos);
	assert(res3.find("3: line 3") != std::string::npos);

	// Test 4: Overlapping Context Lines
	tools::fs_grep_files_args args4;
	args4.pattern = "hello block";
	args4.safe_search_path = temp_dir.string();
	args4.context_lines = 1;
	tools::fs_grep_files_tool tool4(args4);

	doc_prov.mock_lines = {"line 1", "hello block 1", "hello block 2", "line 4"};
	std::string res4 = tool4.execute(ctx);
	std::cout << "RES4:\n" << res4 << std::endl;
	// Should merge into a single block
	assert(res4.find("**Match near Line 1**:") != std::string::npos);
	assert(res4.find("**Match near Line 3**:") == std::string::npos); // Should be merged!
	assert(res4.find("4: line 4") != std::string::npos);

	// Test 5: Duplicate query detection
	{
		// Execute tool twice with identical parameters
		tools::fs_grep_files_args args5;
		args5.pattern = "hello_dup";
		args5.safe_search_path = temp_dir.string();
		args5.search_path = temp_dir.string();

		tools::fs_grep_files_tool tool5a(args5);
		std::string res5a = tool5a.execute(ctx);

		tools::fs_grep_files_tool tool5b(args5);
		std::string res5b = tool5b.execute(ctx);

		std::cout << "RES5B:\n" << res5b << std::endl;
		assert(res5b.find("WARNING: You have already performed this exact search query") != std::string::npos);
	}

	// Test 6: LSP symbol lookup and formatting
	{
		test_fs_grep_files_tool_mocked tool6(args1);
		
		lsp_manager::symbol_info sym1;
		sym1.name = "hello";
		sym1.kind = 12; // Function
		sym1.location.path = (temp_dir / "src/hello.cpp").string();
		sym1.location.range = {41, 0, 41, 10}; // 0-indexed line 41 -> line 42

		lsp_manager::symbol_info sym2;
		sym2.name = "hello_world";
		sym2.kind = 5; // Class
		sym2.location.path = (temp_dir / "src/hello.h").string();
		sym2.location.range = {9, 0, 9, 5}; // 0-indexed line 9 -> line 10

		lsp_manager::symbol_info sym3;
		sym3.name = "hello_layout";
		sym3.kind = 6; // Method
		sym3.location.path = (temp_dir / "src/editor.cpp").string();
		sym3.location.range = {268, 0, 311, 10}; // 0-indexed 268->269, 311->312

		tool6.mock_symbols = {sym1, sym2, sym3};

		std::string res6 = tool6.execute(ctx);
		std::cout << "RES6:\n" << res6 << std::endl;

		assert(res6.find("### LSP Symbol Definitions:") != std::string::npos);
		assert(res6.find("* **Function `hello`** is defined in `src/hello.cpp` at line 42") != std::string::npos);
		assert(res6.find("* **Class `hello_world`** is defined in `src/hello.h` at line 10") != std::string::npos);
		assert(res6.find("* **Method `hello_layout`** is defined in `src/editor.cpp` at line 269 to 312") != std::string::npos);
	}

	// Test 7: Exclude path, ext, and pattern
	{
		fs::path sub_dir = temp_dir / "build_output";
		fs::create_directories(sub_dir);

		fs::path f_log = temp_dir / "app.log";
		std::ofstream(f_log) << "target_search_key inside app log\n";

		fs::path f_build = sub_dir / "output.txt";
		std::ofstream(f_build) << "target_search_key inside build output\n";

		fs::path f_src = temp_dir / "main.cpp";
		std::ofstream(f_src) << "target_search_key inside main cpp\n";

		// Test 7a: exclude_ext = ".log"
		tools::fs_grep_files_args args7a;
		args7a.pattern = "target_search_key";
		args7a.safe_search_path = temp_dir.string();
		args7a.exclude_ext = ".log";
		tools::fs_grep_files_tool tool7a(args7a);

		std::string res7a = tool7a.execute(ctx);
		assert(res7a.find("app.log") == std::string::npos);
		assert(res7a.find("main.cpp") != std::string::npos);

		// Test 7b: exclude_path = "build_output"
		tools::fs_grep_files_args args7b;
		args7b.pattern = "target_search_key";
		args7b.safe_search_path = temp_dir.string();
		args7b.exclude_path = "build_output";
		tools::fs_grep_files_tool tool7b(args7b);

		std::string res7b = tool7b.execute(ctx);
		assert(res7b.find("output.txt") == std::string::npos);
		assert(res7b.find("main.cpp") != std::string::npos);

		// Test 7c: exclude_pattern = ".*main.*"
		tools::fs_grep_files_args args7c;
		args7c.pattern = "target_search_key";
		args7c.safe_search_path = temp_dir.string();
		args7c.exclude_pattern = ".*main.*";
		tools::fs_grep_files_tool tool7c(args7c);

		std::string res7c = tool7c.execute(ctx);
		assert(res7c.find("main.cpp") == std::string::npos);
	}

	// Test 8: Tier priority sorting (Tier 1 core source vs Tier 4 backup tilde file)
	{
		fs::path backup_file = temp_dir / "target.cpp~";
		std::ofstream(backup_file) << "priority_key line inside backup tilde file\n";

		fs::path source_file = temp_dir / "target.cpp";
		std::ofstream(source_file) << "priority_key line inside main source file\n";

		tools::fs_grep_files_args args8;
		args8.pattern = "priority_key";
		args8.safe_search_path = temp_dir.string();
		args8.limit = 1; // Limit to 1 match so only top tier file gets detailed snippet
		tools::fs_grep_files_tool tool8(args8);

		std::string res8 = tool8.execute(ctx);
		assert(res8.find("### `target.cpp`") != std::string::npos);
		assert(res8.find("target.cpp~") != std::string::npos); // Should be in overflow list
		assert(res8.find("### `target.cpp~`") == std::string::npos); // Should NOT be in detailed matches header!
	}

	// Test 9: Enclosing scope annotation formatting with line range L{}-{}
	{
		fs::path cpp_file = temp_dir / "scope_test.cpp";
		{
			std::ofstream out(cpp_file);
			out << "void my_target_function() {\n";
			out << "    int val = 42;\n";
			out << "    return_scope_key();\n";
			out << "}\n";
		}

		tools::fs_grep_files_args args9;
		args9.pattern = "return_scope_key";
		args9.safe_search_path = temp_dir.string();
		tools::fs_grep_files_tool tool9(args9);

		std::string res9 = tool9.execute(ctx);
		assert(res9.find("[in Function `my_target_function` L1-4]") != std::string::npos);
	}

	// Test 11: case_insensitive flag (mirrors fs_regexp_lines)
	{
		fs::path ci_file = temp_dir / "case_sense.txt";
		std::ofstream out(ci_file);
		out << "Hello TurboStar\n";
		out << "goodbye world\n";
		out.close();

		// 10a. Literal, case-insensitive: matching "hello" should find the "Hello" line
		{
			tools::fs_grep_files_args args10a;
			args10a.pattern = "hello";
			args10a.case_insensitive = true;
			args10a.safe_search_path = temp_dir.string();
			tools::fs_grep_files_tool tool10a(args10a);
			std::string res10a = tool10a.execute(ctx);
			assert(res10a.find("Hello TurboStar") != std::string::npos);
		}

		// 10b. Literal, case-sensitive (default): "hello" should NOT match "Hello"
		{
			tools::fs_grep_files_args args10b;
			args10b.pattern = "hello";
			args10b.case_insensitive = false;
			args10b.safe_search_path = temp_dir.string();
			tools::fs_grep_files_tool tool10b(args10b);
			std::string res10b = tool10b.execute(ctx);
			assert(res10b.find("Hello TurboStar") == std::string::npos);
		}

		// 10c. Regex, case-insensitive with is_regex=true
		{
			tools::fs_grep_files_args args10c;
			args10c.pattern = "TURBOSTAR";
			args10c.is_regex = true;
			args10c.case_insensitive = true;
			args10c.safe_search_path = temp_dir.string();
			tools::fs_grep_files_tool tool10c(args10c);
			std::string res10c = tool10c.execute(ctx);
			assert(res10c.find("Hello TurboStar") != std::string::npos);
		}
	}

	fs::remove_all(temp_dir);

	// Verify description mentions grep
	tools::fs_grep_files_validator val;
	assert(val.get_description().find("grep") != std::string::npos);

	// Test 10: Phase-1 schema accepts both 'path' and 'search_path' aliases
	{
		fs::path tmp2 = temp_dir / "alias_test";
		fs::create_directories(tmp2);
		std::ofstream out(tmp2 / "target.txt");
		out << "alias_keyword_match\n";
		out.close();

		agentlib::tool_context vctx;
		vctx.fs_security.set_working_directory(tmp2);
		vctx.fs_security.add_allowed_root(tmp2, agentlib::access_type::read);

		// 10a. 'search_path' (original name) must still validate
		nlohmann::json with_search_path = {{"pattern", "alias_keyword_match"}, {"search_path", "."}};
		std::string err;
		tools::fs_grep_files_validator v1;
		assert(v1.validate_args(with_search_path, vctx, err));
		assert(err.empty());

		// 10b. 'path' alias must validate and carry through to execution
		nlohmann::json with_path = {{"pattern", "alias_keyword_match"}, {"path", "."}};
		std::string err2;
		tools::fs_grep_files_validator v2;
		assert(v2.validate_args(with_path, vctx, err2));
		assert(err2.empty());
		auto tool2 = v2.create_tool(with_path);
		assert(tool2 != nullptr);
		std::string res2 = tool2->execute(vctx);
		assert(res2.find("Found 1 matches") != std::string::npos);
		assert(res2.find("target.txt") != std::string::npos);

		// 10c. 'query' alias for 'pattern' must validate and carry through to execution
		nlohmann::json with_query = {{"query", "different_alias_keyword"}, {"path", "."}};
		std::ofstream out2(tmp2 / "target2.txt");
		out2 << "different_alias_keyword\n";
		out2.close();
		std::string err3;
		tools::fs_grep_files_validator v3;
		assert(v3.validate_args(with_query, vctx, err3));
		assert(err3.empty());
		auto tool3 = v3.create_tool(with_query);
		assert(tool3 != nullptr);
		std::string res3 = tool3->execute(vctx);
		assert(res3.find("Found 1 matches") != std::string::npos);
		assert(res3.find("target2.txt") != std::string::npos);

		fs::remove_all(tmp2);
	}

	std::cout << "fs_grep_files unit test passed!\n";
	return 0;
}
