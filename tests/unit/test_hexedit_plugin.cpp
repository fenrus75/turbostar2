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

void test_named_offsets()
{
	std::string test_file = "test_named_offsets_temp.png";
	{
		std::ofstream out(test_file, std::ios::binary);
		// PNG Signature (8 bytes)
		const char sig[] = "\x89PNG\r\n\x1a\n";
		out.write(sig, 8);

		// IHDR Chunk: len=13, type="IHDR", data=13 zeros, crc=4 zeros (total 25 bytes)
		const char ihdr_header[] = "\x00\x00\x00\x0dIHDR";
		out.write(ihdr_header, 8);
		const char zeros[13] = {0};
		out.write(zeros, 13);
		const char crc[] = "\x00\x00\x00\x00";
		out.write(crc, 4);

		// PLTE Chunk: len=6, type="PLTE", data=6 zeros, crc=4 zeros (total 18 bytes)
		const char plte_header[] = "\x00\x00\x00\x06PLTE";
		out.write(plte_header, 8);
		out.write(zeros, 6);
		out.write(crc, 4);

		// IEND Chunk: len=0, type="IEND", data=0, crc=4 zeros
		const char iend_header[] = "\x00\x00\x00\x00IEND";
		out.write(iend_header, 8);
		out.write(crc, 4);
	}

	auto ctx = create_mock_context();

	// Verify Hexdump with offset_by_name
	{
		tools::hexdump_args args;
		args.requested_path = test_file;
		args.safe_path = test_file;
		args.start_offset = 0;
		args.size = 8;
		args.offset_by_name = "PLTE"; // should resolve to offset 33

		tools::hexdump_tool tool(args);
		std::string result = tool.execute(ctx);
		// Verification: Address row starts at 32 (multiple of 16), PLTE starts at 33, so we check "00000020:" is printed
		assert(result.find("00000020:") != std::string::npos);
	}

	// Verify Hexwrite with offset_by_name
	{
		tools::hexwrite_args args;
		args.requested_path = test_file;
		args.safe_path = test_file;
		args.start_offset = 0;
		args.hex_data = "11 22 33 44";
		args.offset_by_name = "PLTE"; // should resolve to offset 33

		tools::hexwrite_tool tool(args);
		std::string result = tool.execute(ctx);
		assert(result.find("Successfully wrote 4 bytes") != std::string::npos);

		// Read back and verify bytes at offset 33
		std::ifstream in(test_file, std::ios::binary);
		in.seekg(33);
		char bytes[4];
		in.read(bytes, 4);
		assert(bytes[0] == 0x11);
		assert(bytes[1] == 0x22);
		assert(bytes[2] == 0x33);
		assert(bytes[3] == 0x44);
	}

	std::filesystem::remove(test_file);
	std::cout << "test_named_offsets passed!" << std::endl;
}

int main()
{
	test_watchdog::setup_watchdog(30);
	test_hexdump_tool();
	test_hexwrite_tool();
	test_named_offsets();
	std::cout << "All hexedit plugin unit tests passed!" << std::endl;
	return 0;
}
