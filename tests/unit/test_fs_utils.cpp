#include <cassert>
#include <iostream>
#include <filesystem>
#include <fstream>
#include "../../src/fs_utils.h"

namespace fs = std::filesystem;

int main()
{
	fs::path temp_dir = fs::temp_directory_path() / "turbostar_test_fs_utils";
	fs::create_directories(temp_dir);

	// 1. Text file (should return false)
	fs::path txt_file = temp_dir / "test.txt";
	{
		std::ofstream out(txt_file);
		out << "Hello, world! This is a plain text file with no null bytes.\n";
	}
	assert(!fs_utils::is_binary_file(txt_file.string()));

	// 2. Binary file (should return true)
	fs::path bin_file = temp_dir / "test.bin";
	{
		std::ofstream out(bin_file, std::ios::binary);
		out << "Hello";
		out.put('\0');
		out << "World";
	}
	assert(fs_utils::is_binary_file(bin_file.string()));

	// 2b. Binary file with control characters but no NUL bytes (should return true)
	fs::path bin_ctrl_file = temp_dir / "test_ctrl.bin";
	{
		std::ofstream out(bin_ctrl_file, std::ios::binary);
		out << "Hello";
		out.put('\x01');
		out.put('\x02');
		out << "World";
	}
	assert(fs_utils::is_binary_file(bin_ctrl_file.string()));

	// 3. Empty file (should return false)
	fs::path empty_file = temp_dir / "empty.bin";
	{
		std::ofstream out(empty_file, std::ios::binary);
	}
	assert(!fs_utils::is_binary_file(empty_file.string()));

	// 4. Non-existent file (should return false)
	fs::path non_existent = temp_dir / "does_not_exist.txt";
	assert(!fs_utils::is_binary_file(non_existent.string()));

	// 5. Directory (should return false)
	assert(!fs_utils::is_binary_file(temp_dir.string()));

	// Cleanup
	fs::remove_all(temp_dir);

	std::cout << "All fs_utils unit tests passed!" << std::endl;
	return 0;
}
