#include <cassert>
#include <iostream>
#include <memory>
#include <filesystem>
#include <fstream>
#include "../../src/agentlib/tool_context.h"
#include "../../src/agentlib/document_provider.h"
#include "../../src/tools/fs_find_in_files/fs_find_in_files.h"

namespace fs = std::filesystem;

class mock_document_snapshot : public agentlib::document_snapshot {
public:
    std::vector<std::string> lines;
    
    size_t get_line_count() const override { return lines.size(); }
    std::string get_line_text(size_t index) const override { return lines[index]; }
    std::vector<agentlib::diagnostic_snapshot> get_diagnostics() const override { return {}; }
};

class mock_document_provider : public agentlib::document_provider {
public:
    std::string mock_open_file;
    std::vector<std::string> mock_lines;

    std::vector<std::string> get_open_document_paths() const override {
        if (!mock_open_file.empty()) return {mock_open_file};
        return {};
    }

    std::unique_ptr<agentlib::document_snapshot> get_open_document(const std::string& safe_path) const override {
        if (safe_path == mock_open_file) {
            auto snap = std::make_unique<mock_document_snapshot>();
            snap->lines = mock_lines;
            return snap;
        }
        return nullptr;
    }

    bool apply_live_edits(const std::string& /*safe_path*/, const std::string& /*edits_json_payload*/) override {
        return false;
    }
    
    void save_all_documents() override {}
};

int main() {
    fs::path temp_dir = fs::temp_directory_path() / "turbostar_test_find";
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
    tools::fs_find_in_files_args args1;
    args1.pattern = "hello";
    args1.safe_dir_path = temp_dir.string();
    tools::fs_find_in_files_tool tool1(args1);
    
    std::string res1 = tool1.execute(ctx);
    assert(res1.find("Found 1 matches across 1 files") != std::string::npos);
    assert(res1.find("Line 2") != std::string::npos);

    // Test 2: Editor buffer search
    mock_document_provider doc_prov;
    doc_prov.mock_open_file = file1.string();
    doc_prov.mock_lines = {"line 1 modified", "hello dirty buffer", "line 3"};
    ctx.doc_provider = &doc_prov;

    std::string res2 = tool1.execute(ctx);
    assert(res2.find("Found 1 matches across 1 files") != std::string::npos);
    assert(res2.find("dirty buffer") != std::string::npos); // Should match the mock buffer, not disk


    // Test 3: Context Lines
    tools::fs_find_in_files_args args3;
    args3.pattern = "hello";
    args3.safe_dir_path = temp_dir.string();
    args3.context_lines = 1;
    tools::fs_find_in_files_tool tool3(args3);
    
    std::string res3 = tool3.execute(ctx);
    std::cout << "RES3:\n" << res3 << std::endl;
    assert(res3.find("Match near Line 1:") != std::string::npos);
    assert(res3.find("1: line 1 modified") != std::string::npos);
    assert(res3.find("3: line 3") != std::string::npos);

    // Test 4: Overlapping Context Lines
    doc_prov.mock_lines = {"line 1", "hello block 1", "hello block 2", "line 4"};
    std::string res4 = tool3.execute(ctx);
    std::cout << "RES4:\n" << res4 << std::endl;
    // Should merge into a single block
    assert(res4.find("Match near Line 1:") != std::string::npos);
    assert(res4.find("Match near Line 3:") == std::string::npos); // Should be merged!
    assert(res4.find("4: line 4") != std::string::npos);

    fs::remove_all(temp_dir);
    std::cout << "fs_find_in_files unit test passed!\n";
    return 0;
}
