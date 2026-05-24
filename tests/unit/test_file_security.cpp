#include <cassert>
#include <fstream>
#include <iostream>
#include "../../src/agentlib/file_security_manager.h"

using namespace agentlib;

void create_dummy_file(const std::filesystem::path &path)
{
	std::ofstream f(path);
	f << "dummy";
}

int main()
{
	file_security_manager fsm;
	std::string out_path;
	std::string out_err;

	// Create a temporary workspace
	std::filesystem::path tmp_workspace = std::filesystem::temp_directory_path() / "fsm_test_workspace";
	std::filesystem::create_directories(tmp_workspace);
	std::filesystem::path tmp_readonly = std::filesystem::temp_directory_path() / "fsm_test_readonly";
	std::filesystem::create_directories(tmp_readonly);

	// Setup FSM
	fsm.set_working_directory(tmp_workspace);
	fsm.add_allowed_root(tmp_workspace, access_type::write);
	fsm.add_allowed_root(tmp_readonly, access_type::read);
	fsm.add_ignore_pattern(".git");

	// 1. Basic allowed read in workspace
	assert(fsm.validate_access("test.txt", access_type::read, out_path, out_err) == true);
	assert(out_path == (std::filesystem::weakly_canonical(tmp_workspace) / "test.txt").string());

	// 2. Absolute path in workspace
	assert(fsm.validate_access((tmp_workspace / "absolute.txt").string(), access_type::write, out_path, out_err) == true);

	// 3. Directory Traversal (escaping workspace)
	assert(fsm.validate_access("../../etc/passwd", access_type::read, out_path, out_err) == false);

	// 4. Read-only root check
	assert(fsm.validate_access((tmp_readonly / "include.h").string(), access_type::read, out_path, out_err) == true);
	// Write should be denied in read-only root
	assert(fsm.validate_access((tmp_readonly / "include.h").string(), access_type::write, out_path, out_err) == false);

	// 5. Ignore patterns
	assert(fsm.validate_access(".git/config", access_type::read, out_path, out_err) == false);

	// Cleanup
	std::filesystem::remove_all(tmp_workspace);
	std::filesystem::remove_all(tmp_readonly);

	std::cout << "file_security_manager unit tests passed!\n";
	return 0;
}
