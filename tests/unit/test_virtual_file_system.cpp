#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "../../src/agentlib/virtual_file_system.h"

using namespace agentlib;

void test_basic_mount_and_read()
{
	virtual_file_system vfs;

	// Create a temporary file
	std::string temp_path = "test_vfs_temp.txt";
	std::string content = "Hello, Virtual File System!";
	{
		std::ofstream out(temp_path);
		out << content;
	}

	// Mount it
	std::string uri = "skills://test/hello.txt";
	bool success = vfs.mount_file(uri, temp_path);
	if (!success) {
		std::cerr << "Failed to mount file\n";
		assert(success);
	}
	assert(vfs.exists(uri));

	// Read it
	auto view_opt = vfs.read_file(uri);
	assert(view_opt.has_value());
	assert(view_opt.value()->view() == content);

	// Get info
	auto info_opt = vfs.get_file_info(uri);
	assert(info_opt.has_value());
	assert(info_opt->size == content.size());

	// Clean up
	vfs.unmount_file(uri);
	assert(!vfs.exists(uri));
	std::filesystem::remove(temp_path);
}

void test_directory_listing()
{
	virtual_file_system vfs;

	// Create temp files
	std::filesystem::create_directory("test_vfs_dir");

	std::string f1 = "test_vfs_dir/f1.txt";
	std::string f2 = "test_vfs_dir/f2.txt";
	std::string f3 = "test_vfs_dir/f3.txt";

	{
		std::ofstream out(f1);
		out << "1";
	}
	{
		std::ofstream out(f2);
		out << "22";
	}
	{
		std::ofstream out(f3);
		out << "333";
	}

	vfs.mount_file("skills://dir/f1.txt", f1);
	vfs.mount_file("skills://dir/f2.txt", f2);
	vfs.mount_file("skills://dir/f3.txt", f3);
	vfs.mount_file("skills://other/f4.txt", f1);

	auto list = vfs.list_directory("skills://dir/");
	// Expect 4: skills://dir/, skills://dir/f1.txt, skills://dir/f2.txt, skills://dir/f3.txt
	assert(list.size() == 4);

	// Unmount prefix
	vfs.unmount_prefix("skills://dir/");
	assert(!vfs.exists("skills://dir/f1.txt"));
	assert(vfs.exists("skills://other/f4.txt"));

	std::filesystem::remove_all("test_vfs_dir");
}

void test_line_count()
{
	virtual_file_system vfs;

	// Test buffer line counts
	// 1. Buffer without newline
	assert(vfs.mount_buffer("skills://test/no_newline.txt", "abc"));
	auto info1 = vfs.get_file_info("skills://test/no_newline.txt");
	assert(info1->size_in_lines == 1);

	// 2. Buffer with trailing newline
	assert(vfs.mount_buffer("skills://test/trailing_newline.txt", "abc\n"));
	auto info2 = vfs.get_file_info("skills://test/trailing_newline.txt");
	assert(info2->size_in_lines == 1);

	// 3. Buffer with multiple lines
	assert(vfs.mount_buffer("skills://test/multiple_lines.txt", "abc\ndef\n"));
	auto info3 = vfs.get_file_info("skills://test/multiple_lines.txt");
	assert(info3->size_in_lines == 2);
}

int main()
{
	test_basic_mount_and_read();
	test_directory_listing();
	test_line_count();
	std::cout << "virtual_file_system tests passed.\n";
	return 0;
}
