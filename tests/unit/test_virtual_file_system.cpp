// Tested source file: src/agentlib/virtual_file_system.cpp
#include "test_watchdog.h"
#include <cassert>
#include <filesystem>

#include <fstream>
#include <iostream>
#include <cstring>
#include "agentlib/virtual_file_system.h"
#include "project_manager.h"
#include "images/image_manager.h"
#include "fs_utils.h"

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

void test_file_provider()
{
	virtual_file_system vfs;

	// Create a temp file on disk
	std::filesystem::path temp_dir = std::filesystem::absolute("test_vfs_file_provider_dir");
	std::filesystem::create_directory(temp_dir);

	std::filesystem::path temp_file = temp_dir / "test_file.txt";
	std::string content = "Local file VFS content test!";
	{
		std::ofstream out(temp_file);
		out << content;
	}

	std::string file_uri = "file://" + temp_file.string();
	std::string dir_uri = "file://" + temp_dir.string();

	// Test exists
	assert(vfs.exists(file_uri));
	assert(vfs.exists(dir_uri));
	assert(!vfs.exists(file_uri + "_nonexistent"));

	// Test read_file
	auto handle_opt = vfs.read_file(file_uri);
	assert(handle_opt.has_value());
	assert((*handle_opt)->view() == content);

	// Test get_file_info
	auto info_opt = vfs.get_file_info(file_uri);
	assert(info_opt.has_value());
	assert(info_opt->size == content.size());
	assert(info_opt->type == 'F');

	auto dir_info_opt = vfs.get_file_info(dir_uri);
	assert(dir_info_opt.has_value());
	assert(dir_info_opt->type == 'D');

	// Test list_directory
	auto list = vfs.list_directory(dir_uri);
	assert(list.size() == 1);
	assert(list[0].uri == file_uri);
	assert(list[0].size == content.size());
	assert(list[0].type == 'F');

	// Clean up
	std::filesystem::remove_all(temp_dir);
}

void test_file_vfs_write()
{
	virtual_file_system vfs;

	// Determine workspace directory (project dir)
	std::string proj_dir = fs_utils::get_project_dir();
	assert(!proj_dir.empty());

	// 1. Write inside workspace using stream (create_file)
	std::string target_file_path = proj_dir + "/test_vfs_write_stream.txt";
	std::string target_uri = "file://" + target_file_path;
	
	auto writer_opt = vfs.create_file(target_uri);
	if (!writer_opt.has_value()) {
		std::cerr << "Failed to create write stream for target: " << target_uri << "\n";
	}
	assert(writer_opt.has_value());
	auto writer = std::move(*writer_opt);
	
	std::string chunk1 = "Hello ";
	std::string chunk2 = "VFS Write!";
	assert(writer->write(chunk1.data(), chunk1.size()));
	assert(writer->write(chunk2.data(), chunk2.size()));
	std::string result_desc = writer->close();
	assert(!result_desc.empty());
	assert(std::filesystem::exists(target_file_path));

	// Verify content
	auto reader_opt = vfs.read_file(target_uri);
	assert(reader_opt.has_value());
	assert((*reader_opt)->view() == "Hello VFS Write!");

	std::filesystem::remove(target_file_path);

	// 2. Write inside workspace using single-step helper (write_file)
	std::string target_file_path2 = proj_dir + "/test_vfs_write_shortcut.txt";
	std::string target_uri2 = "file://" + target_file_path2;
	std::string blob = "Shortcut Write!";
	std::string shortcut_desc = vfs.write_file(target_uri2, blob.data(), blob.size());
	assert(!shortcut_desc.empty());
	assert(std::filesystem::exists(target_file_path2));

	auto reader_opt2 = vfs.read_file(target_uri2);
	assert(reader_opt2.has_value());
	assert((*reader_opt2)->view() == blob);

	std::filesystem::remove(target_file_path2);

	// 3. Attempt write outside workspace (should fail/block)
	std::string bad_file_path = "/tmp/should_fail_vfs_write.txt";
	std::string bad_uri = "file://" + bad_file_path;
	auto bad_writer_opt = vfs.create_file(bad_uri);
	assert(!bad_writer_opt.has_value());
}

void test_images_vfs_write()
{
	virtual_file_system vfs;

	// Ingest a dummy image file content via VFS
	// Using a valid 1x1 PNG pixel representation
	static const unsigned char tiny_png[] = {
		0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
		0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
		0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
		0x0a, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x00, 0x01, 0x00, 0x00,
		0x05, 0x00, 0x01, 0x0d, 0x0a, 0x2d, 0xb4, 0x00, 0x00, 0x00, 0x00, 0x49,
		0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82
	};

	std::string image_uri = "images://by-name/vfs-test-image.png";

	// Write the image via write_file VFS method
	std::string desc = vfs.write_file(image_uri, tiny_png, sizeof(tiny_png));
	assert(!desc.empty());

	// Verify image exists in VFS
	assert(vfs.exists(image_uri));

	// Verify we can read it back
	auto handle_opt = vfs.read_file(image_uri);
	assert(handle_opt.has_value());
	std::string_view read_view = (*handle_opt)->view();
	assert(read_view.size() == sizeof(tiny_png));
	assert(std::memcmp(read_view.data(), tiny_png, sizeof(tiny_png)) == 0);

	// Retrieve file info
	auto info_opt = vfs.get_file_info(image_uri);
	assert(info_opt.has_value());
	assert(info_opt->size == sizeof(tiny_png));

	// Cleanup
	images::image_manager::get_instance().delete_image(image_uri);
}

void test_unsupported_and_invalid_uris()
{
	virtual_file_system vfs;

	std::string bad_scheme_uri = "unknown_scheme://foo/bar";
	assert(!vfs.exists(bad_scheme_uri));
	assert(!vfs.read_file(bad_scheme_uri).has_value());
	assert(!vfs.get_file_info(bad_scheme_uri).has_value());
	assert(!vfs.create_file(bad_scheme_uri).has_value());
	assert(vfs.write_file(bad_scheme_uri, "data", 4).empty());
	assert(vfs.list_directory(bad_scheme_uri).empty());

	std::string no_scheme_uri = "just_a_path/file.txt";
	assert(!vfs.exists(no_scheme_uri));
	assert(!vfs.read_file(no_scheme_uri).has_value());
}

void test_http_vfs_provider()
{
	virtual_file_system vfs;

	// Unreachable HTTP URL should return std::nullopt cleanly
	std::string bad_http_uri = "http://127.0.0.1:59999/nonexistent_test_file.txt";
	assert(!vfs.exists(bad_http_uri));
	assert(!vfs.read_file(bad_http_uri).has_value());
	assert(!vfs.get_file_info(bad_http_uri).has_value());

	// Unreachable HTTPS URL should return std::nullopt cleanly
	std::string bad_https_uri = "https://127.0.0.1:59999/nonexistent_test_file.txt";
	assert(!vfs.exists(bad_https_uri));
	assert(!vfs.read_file(bad_https_uri).has_value());
}

void test_memory_vfs_edge_cases()
{
	virtual_file_system vfs;

	// 1. Mount empty buffer
	std::string empty_uri = "skills://test/empty.txt";
	assert(vfs.mount_buffer(empty_uri, ""));
	assert(vfs.exists(empty_uri));
	auto info_empty = vfs.get_file_info(empty_uri);
	assert(info_empty.has_value());
	assert(info_empty->size == 0);
	assert(info_empty->size_in_lines == 0);
	auto read_empty = vfs.read_file(empty_uri);
	assert(read_empty.has_value());
	assert((*read_empty)->view().empty());

	// 2. Mount CRLF buffer
	std::string crlf_uri = "skills://test/crlf.txt";
	assert(vfs.mount_buffer(crlf_uri, "line1\r\nline2\r\nline3\r\n"));
	auto info_crlf = vfs.get_file_info(crlf_uri);
	assert(info_crlf.has_value());
	assert(info_crlf->size_in_lines == 3);

	// 3. Unmount non-existent URI (should handle gracefully)
	vfs.unmount_file("skills://test/does_not_exist.txt");
	vfs.unmount_prefix("skills://nonexistent_prefix/");
}

void test_include_vfs_provider()
{
	virtual_file_system vfs;
	assert(vfs.exists("include://stdio.h"));
	assert(vfs.is_local_path_available("include://stdio.h"));
	std::string loc = vfs.get_local_path("include://stdio.h");
	assert(loc.starts_with("/usr/include"));

	auto handle = vfs.read_file("include://stdio.h");
	assert(handle.has_value());
	assert((*handle)->view().size() > 0);


	// Test C++ header resolution
	assert(vfs.exists("include://vector"));
	auto v_handle = vfs.read_file("include://vector");
	assert(v_handle.has_value());

	// Test non-existent header
	assert(!vfs.exists("include://non_existent_header_12345.h"));
}

int main()
{
	test_watchdog::setup_watchdog(30);
	project_manager::get_instance().initialize();
	images::image_manager::get_instance().initialize();

	test_basic_mount_and_read();
	test_directory_listing();
	test_line_count();
	test_file_provider();
	test_file_vfs_write();
	test_images_vfs_write();
	test_unsupported_and_invalid_uris();
	test_http_vfs_provider();
	test_memory_vfs_edge_cases();
	test_include_vfs_provider();
	std::cout << "virtual_file_system tests passed.\n";
	return 0;
}


