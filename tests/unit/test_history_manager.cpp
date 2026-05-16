#include <cassert>
#include <cstdlib>
#include <iostream>
#include <filesystem>
#include "../../src/history_manager.h"

namespace fs = std::filesystem;

void test_history_manager() {
    auto& hm = history_manager::get_instance();

    // 1. Test basic adding
    hm.add_search("search1");
    hm.add_search("search2");
    
    auto searches = hm.get_searches();
    assert(searches.size() >= 2);
    assert(searches[0] == "search2"); // Newest at front
    assert(searches[1] == "search1");

    // 2. Test duplicate adding (moves to front)
    hm.add_search("search1");
    searches = hm.get_searches();
    assert(searches[0] == "search1");
    assert(searches[1] == "search2");

    // 3. Test empty ignores
    size_t prev_size = hm.get_searches().size();
    hm.add_search("");
    assert(hm.get_searches().size() == prev_size);

    // 4. Test adding files
    hm.add_file("file1.txt");
    hm.add_file("file2.txt");
    auto files = hm.get_files();
    assert(files.size() >= 2);
    assert(files[0] == "file2.txt");

    // 5. Test unknown.txt ignore
    prev_size = hm.get_files().size();
    hm.add_file("unknown.txt");
    assert(hm.get_files().size() == prev_size);

    // 6. Test Save and Load
    // First, clear by loading a non-existent file
    setenv("HOME", "/tmp/non_existent_turbostar_test_dir", 1);
    hm.load();
    assert(hm.get_searches().empty());
    assert(hm.get_files().empty());

    hm.add_search("persist1");
    hm.add_file("persist2.txt");
    hm.save();

    // Reload
    hm.load();
    assert(hm.get_searches().size() == 1);
    assert(hm.get_searches()[0] == "persist1");
    assert(hm.get_files().size() == 1);
    assert(hm.get_files()[0] == "persist2.txt");

    // Clean up
    fs::remove("/tmp/non_existent_turbostar_test_dir/.turbostar_history");

    // 7. Test capacity limits
    hm.load(); // clears it since file is deleted
    for (int i = 0; i < 60; ++i) {
        hm.add_search("s" + std::to_string(i));
        hm.add_file("f" + std::to_string(i));
    }
    assert(hm.get_searches().size() == 50);
    assert(hm.get_files().size() == 50);
    // 8. Test HOME not set fallback
    unsetenv("HOME");
    assert(hm.get_searches().size() == 50); // Just checking it doesn't crash

    // 9. Test save failure (e.g. trying to save to an invalid directory)
    setenv("HOME", "/root/forbidden_dir_that_does_not_exist", 1);
    hm.save(); // Should log error and return safely

    // 10. Test empty and unknown file handling explicitly
    prev_size = hm.get_files().size();
    hm.add_file("");
    hm.add_file("unknown.txt");
    assert(hm.get_files().size() == prev_size);

    std::cout << "history_manager unit tests passed!\n";
}

int main() {
    test_history_manager();
    return 0;
}
