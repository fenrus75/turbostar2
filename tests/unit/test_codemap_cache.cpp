// Tested source file: src/codemap_cache.cpp
#include "test_watchdog.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "codemap_cache.h"
#include "codemap_utils.h"
#include "fs_utils.h"

using namespace tools;

int main()
{
	test_watchdog::setup_watchdog(30);

	std::string test_dir = (std::filesystem::temp_directory_path() / "turbostar_test_codemap_cache").string();
	std::filesystem::remove_all(test_dir);
	std::filesystem::create_directories(test_dir);

	std::string test_file_1 = (std::filesystem::path(test_dir) / "file1.cpp").string();
	std::string test_file_2 = (std::filesystem::path(test_dir) / "file2.cpp").string();
	std::string test_cache_file = (std::filesystem::path(test_dir) / "cache.json").string();

	{
		std::ofstream f1(test_file_1);
		f1 << "void foo() {\n    // line 2\n    // line 3\n}\n";
	}
	{
		std::ofstream f2(test_file_2);
		f2 << "class Bar {\n    int x;\n};\n";
	}

	auto &cache = codemap_cache::get_instance();
	cache.clear();
	cache.set_cache_file_override(test_cache_file);

	// ==========================================
	// Test 1: Basic put and get with min_lines
	// ==========================================
	std::cout << "[Test 1] Basic put and get..." << std::endl;
	{
		std::vector<codemap_symbol_info> syms;
		syms.push_back({"foo", "foo", "Function", 1, 4, 4, 0, ""});
		syms.push_back({"small_sym", "small_sym", "Variable", 2, 2, 1, 1, ""});

		cache.put(test_file_1, syms);
		assert(cache.size() == 1);
		assert(cache.has_valid_entry(test_file_1));

		std::vector<codemap_symbol_info> retrieved;
		bool hit = cache.get(test_file_1, 1, retrieved);
		assert(hit);
		assert(retrieved.size() == 2);
		assert(retrieved[0].name == "foo");
		assert(retrieved[1].name == "small_sym");

		// Filter by min_lines = 3
		std::vector<codemap_symbol_info> filtered;
		hit = cache.get(test_file_1, 3, filtered);
		assert(hit);
		assert(filtered.size() == 1);
		assert(filtered[0].name == "foo");
		assert(filtered[0].line_count == 4);
	}

	// ==========================================
	// Test 2: Mtime and file size invalidation
	// ==========================================
	std::cout << "[Test 2] Mtime invalidation..." << std::endl;
	{
		// Sleep slightly to guarantee different filesystem timestamp
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
		{
			std::ofstream f1(test_file_1, std::ios::app);
			f1 << "\n// extra line\n";
		}

		std::vector<codemap_symbol_info> retrieved;
		bool hit = cache.get(test_file_1, 1, retrieved);
		assert(!hit); // File changed on disk, must miss
		assert(!cache.has_valid_entry(test_file_1));
	}

	// ==========================================
	// Test 3: LRU Eviction
	// ==========================================
	std::cout << "[Test 3] LRU eviction..." << std::endl;
	{
		cache.clear();

		std::vector<codemap_symbol_info> syms1 = {{"foo", "foo", "Function", 1, 4, 4, 0, ""}};
		std::vector<codemap_symbol_info> syms2 = {{"Bar", "Bar", "Class", 1, 3, 3, 0, ""}};

		cache.put(test_file_1, syms1);
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		cache.put(test_file_2, syms2);

		assert(cache.size() == 2);

		// Prune to 1 entry: test_file_1 was used earlier, so it should be evicted
		cache.prune_lru(1);
		assert(cache.size() == 1);
		assert(cache.has_valid_entry(test_file_2));
		assert(!cache.has_valid_entry(test_file_1));
	}

	// ==========================================
	// Test 4: Persistence save and reload
	// ==========================================
	std::cout << "[Test 4] Persistence save and reload..." << std::endl;
	{
		cache.clear();

		std::vector<codemap_symbol_info> syms1 = {{"foo", "foo", "Function", 1, 4, 4, 0, ""}};
		std::vector<codemap_symbol_info> syms2 = {{"Bar", "Bar", "Class", 1, 3, 3, 0, ""}};

		cache.put(test_file_1, syms1);
		cache.put(test_file_2, syms2);

		cache.save_to_disk();
		assert(std::filesystem::exists(test_cache_file));

		// Clear in-memory state
		cache.clear();
		assert(cache.size() == 0);

		// Reload from disk
		cache.load_from_disk();
		assert(cache.size() == 2);

		std::vector<codemap_symbol_info> retrieved;
		assert(cache.get(test_file_1, 1, retrieved));
		assert(retrieved.size() == 1);
		assert(retrieved[0].name == "foo");

		assert(cache.get(test_file_2, 1, retrieved));
		assert(retrieved.size() == 1);
		assert(retrieved[0].name == "Bar");
	}

	// ==========================================
	// Test 5: Stale file detection on load
	// ==========================================
	std::cout << "[Test 5] Stale file detection on load..." << std::endl;
	{
		// Modify test_file_1 on disk before load
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
		{
			std::ofstream f1(test_file_1, std::ios::app);
			f1 << "// modification\n";
		}

		// Clear and reload
		cache.clear();
		cache.load_from_disk();

		// test_file_1 was modified, so it should be detected as stale and pruned/queued for refresh
		// test_file_2 was unchanged, so it should remain valid
		assert(cache.size() == 1);
		assert(cache.has_valid_entry(test_file_2));
		assert(!cache.has_valid_entry(test_file_1));
	}

	// Cleanup
	std::filesystem::remove_all(test_dir);
	std::cout << "All codemap_cache unit tests passed successfully!" << std::endl;
	return 0;
}
