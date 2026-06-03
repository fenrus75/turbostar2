#include <cassert>
#include <iostream>
#include <filesystem>
#include <fstream>
#include "../../src/fs_utils.h"
#include "../../src/event_logger.h"

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

	// 6. Test set_override_project_dir logging and project directory functions
	std::string test_override_dir = "/tmp/turbostar_dummy_override";
	fs_utils::set_override_project_dir(test_override_dir);

	// Verify get_project_dir returns override
	assert(fs_utils::get_project_dir() == test_override_dir);

	// Verify event logging
	auto match = event_logger::get_instance().get_latest_matching_message("Override project directory set to");
	assert(match.has_value());
	assert(match->find(test_override_dir) != std::string::npos);

	// Verify other project directories resolve with override hash
	std::string tmp_dir = fs_utils::get_project_tmp_dir();
	std::string dump_dir = fs_utils::get_project_dump_dir();
	std::string db_dir = fs_utils::get_project_db_dir();
	std::string history_dir = fs_utils::get_project_history_dir("test");

	std::hash<std::string> hasher;
	std::string hash_str = std::to_string(hasher(test_override_dir));

	assert(tmp_dir.find(hash_str) != std::string::npos);
	assert(dump_dir.find(hash_str) != std::string::npos);
	assert(db_dir.find(hash_str) != std::string::npos);
	assert(history_dir.find(hash_str) != std::string::npos);

	assert(tmp_dir.find("tmp") != std::string::npos);
	assert(dump_dir.find("dumps") != std::string::npos);
	assert(db_dir.find("dbs") != std::string::npos);
	assert(history_dir.find("history/test") != std::string::npos);

	// Reset override project directory to empty
	fs_utils::set_override_project_dir("");

	// Cleanup
	fs::remove_all(temp_dir);

	std::cout << "All fs_utils unit tests passed!" << std::endl;
	return 0;
}
