#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include "../../src/history_manager.h"

namespace fs = std::filesystem;

void test_history_manager()
{
	auto &hm = history_manager::get_instance();

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
	assert(files[0] == fs::absolute("file2.txt").lexically_normal().string());

	// 5. Test unknown.txt ignore
	prev_size = hm.get_files().size();
	hm.add_file("unknown.txt");
	assert(hm.get_files().size() == prev_size);

	// 6. Test Save and Load
	// First, clear by loading a non-existent file
	std::string test_dir = "/tmp/non_existent_turbostar_test_dir";
	fs::create_directories(test_dir);
	setenv("HOME", test_dir.c_str(), 1);
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
	auto files_after = hm.get_files();
	assert(files_after.size() == 1);
	assert(files_after[0] == fs::absolute("persist2.txt").lexically_normal().string());

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

	// 11. Test Cursor Position Persistence and LRU
	setenv("HOME", test_dir.c_str(), 1);
	hm.load();
	hm.set_cursor_pos("cursor_file.txt", 10, 20);
	auto pos = hm.get_cursor_pos("cursor_file.txt");
	assert(pos.has_value());
	assert(pos->x == 10);
	assert(pos->y == 20);

	hm.save();
	hm.load();
	pos = hm.get_cursor_pos("cursor_file.txt");
	assert(pos.has_value());
	assert(pos->x == 10);
	assert(pos->y == 20);

	// Test LRU for cursor memory
	for (int i = 0; i < 30; ++i) {
		hm.set_cursor_pos("file_" + std::to_string(i) + ".txt", i, i);
	}
	// file_0 should be evicted (30 items total, limit 25, 29, 28, 27, 26, 25 are newest)
	// Wait, the order was file_0, file_1, ..., file_29.
	// file_29 is newest. file_0 is oldest.
	assert(!hm.get_cursor_pos("file_0.txt").has_value());
	assert(hm.get_cursor_pos("file_29.txt").has_value());

	// 12. Test path normalization
	hm.set_cursor_pos("./norm.txt", 5, 5);
	std::string norm_abs = fs::absolute("./norm.txt").lexically_normal().string();
	pos = hm.get_cursor_pos(norm_abs);
	assert(pos.has_value());
	assert(pos->x == 5);

	std::cout << "history_manager unit tests passed!\n";
}

int main()
{
	test_history_manager();
	return 0;
}
