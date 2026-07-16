#include "test_watchdog.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>
#include "plugins/hexedit/hexdump_tool.h"
#include "plugins/hexedit/hexwrite_tool.h"
#include "agentlib/file_security_manager.h"
#include "agentlib/tool_registry.h"

// Mock tool context with valid security manager
agentlib::tool_context create_mock_context()
{
	agentlib::tool_context ctx;
	ctx.fs_security.add_allowed_root(std::filesystem::current_path(), agentlib::access_type::read);
	ctx.fs_security.add_allowed_root(std::filesystem::current_path(), agentlib::access_type::write);
	return ctx;
}

void test_hexdump_tool()
{
	std::string test_file = "test_hexdump_temp.bin";
	{
		std::ofstream out(test_file, std::ios::binary);
		// Write 20 bytes: 0 to 19
		for (int i = 0; i < 20; ++i) {
			out.put(static_cast<char>(i));
		}
	}

	tools::hexdump_args args;
	args.requested_path = test_file;
	args.safe_path = test_file;
	args.start_offset = 0;
	args.size = 20;

	tools::hexdump_tool tool(args);
	auto ctx = create_mock_context();
	std::string result = tool.execute(ctx);

	// Verify output contains the hexdump layout
	assert(result.find("00000000:") != std::string::npos);
	assert(result.find("00 01 02 03 04 05 06 07  08 09 0a 0b 0c 0d 0e 0f") != std::string::npos);
	assert(result.find("00000010: 10 11 12 13") != std::string::npos); // partial row padded
	assert(result.find("|................|") != std::string::npos); // ASCII representation

	std::filesystem::remove(test_file);
	std::cout << "test_hexdump_tool passed!" << std::endl;
}

void test_hexwrite_tool()
{
	std::string test_file = "test_hexwrite_temp.bin";
	if (std::filesystem::exists(test_file)) {
		std::filesystem::remove(test_file);
	}

	auto ctx = create_mock_context();

	// Test 1: Write raw hex string to a new file
	{
		tools::hexwrite_args args;
		args.requested_path = test_file;
		args.safe_path = test_file;
		args.start_offset = 0;
		args.hex_data = "7f 45 4c 46";

		tools::hexwrite_tool tool(args);
		std::string result = tool.execute(ctx);
		assert(result.find("Successfully wrote 4 bytes") != std::string::npos);

		// Read back and verify
		std::ifstream in(test_file, std::ios::binary);
		std::vector<char> bytes(4);
		in.read(bytes.data(), 4);
		assert(bytes[0] == 0x7F);
		assert(bytes[1] == 0x45);
		assert(bytes[2] == 0x4C);
		assert(bytes[3] == 0x46);
	}

	// Test 2: Write with 0x prefixes and comma delimiters at offset 8 (auto-grow/pad with 0x00)
	{
		tools::hexwrite_args args;
		args.requested_path = test_file;
		args.safe_path = test_file;
		args.start_offset = 8;
		args.hex_data = "0xaa, 0xbb";

		tools::hexwrite_tool tool(args);
		std::string result = tool.execute(ctx);
		assert(result.find("Successfully wrote 2 bytes") != std::string::npos);

		// Verify size and content
		// size should be 8 (offset) + 2 (size) = 10 bytes
		assert(std::filesystem::file_size(test_file) == 10);

		std::ifstream in(test_file, std::ios::binary);
		std::vector<char> bytes(10);
		in.read(bytes.data(), 10);
		assert(bytes[0] == 0x7F);
		assert(bytes[4] == 0x00); // padding byte
		assert(bytes[7] == 0x00); // padding byte
		assert(bytes[8] == static_cast<char>(0xAA));
		assert(bytes[9] == static_cast<char>(0xBB));
	}

	std::filesystem::remove(test_file);
	std::cout << "test_hexwrite_tool passed!" << std::endl;
}

int main()
{
	test_watchdog::setup_watchdog(30);
	test_hexdump_tool();
	test_hexwrite_tool();
	std::cout << "All hexedit plugin unit tests passed!" << std::endl;
	return 0;
}
