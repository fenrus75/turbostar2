#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
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

	std::unique_ptr<agentlib::document_snapshot> get_open_document(const std::string &safe_path) const override
	{
		if (safe_path == mock_open_file) {
			auto snap = std::make_unique<mock_document_snapshot>();
			snap->lines = mock_lines;
			return snap;
		}
		return nullptr;
	}

	bool apply_live_edits(const std::string & /*safe_path*/, const std::string & /*edits_json_payload*/) override
	{
		return false;
	}

	void save_all_documents() override
	{
	}
};

int main()
{
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
	args1.safe_dir_path = temp_dir.string();
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
	args2.safe_dir_path = temp_dir.string();
	tools::fs_grep_files_tool tool2(args2);

	std::string res2 = tool2.execute(ctx);
	assert(res2.find("Found 1 matches across 1 files") != std::string::npos);
	assert(res2.find("dirty buffer") != std::string::npos); // Should match the mock buffer, not disk

	// Test 3: Context Lines
	tools::fs_grep_files_args args3;
	args3.pattern = "hello";
	args3.safe_dir_path = temp_dir.string();
	args3.context_lines = 1;
	tools::fs_grep_files_tool tool3(args3);

	std::string res3 = tool3.execute(ctx);
	std::cout << "RES3:\n" << res3 << std::endl;
	assert(res3.find("Match near Line 1:") != std::string::npos);
	assert(res3.find("1: line 1 modified") != std::string::npos);
	assert(res3.find("3: line 3") != std::string::npos);

	// Test 4: Overlapping Context Lines
	tools::fs_grep_files_args args4;
	args4.pattern = "hello block";
	args4.safe_dir_path = temp_dir.string();
	args4.context_lines = 1;
	tools::fs_grep_files_tool tool4(args4);

	doc_prov.mock_lines = {"line 1", "hello block 1", "hello block 2", "line 4"};
	std::string res4 = tool4.execute(ctx);
	std::cout << "RES4:\n" << res4 << std::endl;
	// Should merge into a single block
	assert(res4.find("Match near Line 1:") != std::string::npos);
	assert(res4.find("Match near Line 3:") == std::string::npos); // Should be merged!
	assert(res4.find("4: line 4") != std::string::npos);

	// Test 5: Duplicate query detection
	{
		// Execute tool twice with identical parameters
		tools::fs_grep_files_args args5;
		args5.pattern = "hello_dup";
		args5.safe_dir_path = temp_dir.string();
		args5.dir_path = temp_dir.string();

		tools::fs_grep_files_tool tool5a(args5);
		std::string res5a = tool5a.execute(ctx);

		tools::fs_grep_files_tool tool5b(args5);
		std::string res5b = tool5b.execute(ctx);

		std::cout << "RES5B:\n" << res5b << std::endl;
		assert(res5b.find("WARNING: You have already performed this exact search query") != std::string::npos);
	}

	fs::remove_all(temp_dir);

	// Verify description mentions grep
	tools::fs_grep_files_validator val;
	assert(val.get_description().find("grep") != std::string::npos);

	std::cout << "fs_grep_files unit test passed!\n";
	return 0;
}
