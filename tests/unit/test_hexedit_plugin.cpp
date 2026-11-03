// Tested source file: src/plugins/hexedit/hexdump_tool.cpp, src/plugins/hexedit/hexdump_validator.cpp, src/plugins/hexedit/hexwrite_tool.cpp
#include "test_watchdog.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>
#include "plugins/hexedit/hexdump_tool.h"
#include "plugins/hexedit/hexdump_validator.h"
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
	args.offset = 0;
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

static void write_u16_le(std::vector<uint8_t> &buf, size_t offset, uint16_t val)
{
	buf[offset] = val & 0xFF;
	buf[offset + 1] = (val >> 8) & 0xFF;
}

static void write_u32_le(std::vector<uint8_t> &buf, size_t offset, uint32_t val)
{
	buf[offset] = val & 0xFF;
	buf[offset + 1] = (val >> 8) & 0xFF;
	buf[offset + 2] = (val >> 16) & 0xFF;
	buf[offset + 3] = (val >> 24) & 0xFF;
}

static void write_u64_le(std::vector<uint8_t> &buf, size_t offset, uint64_t val)
{
	write_u32_le(buf, offset, val & 0xFFFFFFFF);
	write_u32_le(buf, offset + 4, (val >> 32) & 0xFFFFFFFF);
}

void test_hexdump_elf_code_dedup()
{
	std::string test_file = "test_hexdump_elf_temp.bin";
	std::vector<uint8_t> data(800, 0);
	data[0] = 0x7F; data[1] = 'E'; data[2] = 'L'; data[3] = 'F';
	data[4] = 2; // 64-bit
	data[5] = 1; // LSB
	data[6] = 1;
	write_u16_le(data, 16, 2);  // ET_EXEC
	write_u16_le(data, 18, 62); // EM_X86_64
	write_u32_le(data, 20, 1);
	write_u64_le(data, 40, 128); // e_shoff = 128
	write_u16_le(data, 52, 64);  // e_ehsize = 64
	write_u16_le(data, 58, 64);  // e_shentsize = 64
	write_u16_le(data, 60, 5);   // e_shnum = 5 (null, .text, .symtab, .strtab/.shstrtab, .rodata)
	write_u16_le(data, 62, 3);   // e_shstrndx = 3

	// String table at 500: "\0.text\0.symtab\0.shstrtab\0my_func\0callee_func\0.rodata\0"
	const std::string strtab("\0.text\0.symtab\0.shstrtab\0my_func\0callee_func\0.rodata\0", 53);
	for (size_t i = 0; i < strtab.size(); ++i) {
		data[500 + i] = static_cast<uint8_t>(strtab[i]);
	}

	// Section 1: .text at offset 448 (0x1C0), size 32
	write_u32_le(data, 128 + 64, 1);         // sh_name = ".text"
	write_u32_le(data, 128 + 64 + 4, 1);     // SHT_PROGBITS
	write_u64_le(data, 128 + 64 + 8, 6);     // SHF_ALLOC | SHF_EXECINSTR
	write_u64_le(data, 128 + 64 + 16, 0x401000);
	write_u64_le(data, 128 + 64 + 24, 448);  // sh_offset = 448
	write_u64_le(data, 128 + 64 + 32, 32);   // sh_size = 32

	// Section 2: .symtab at offset 600, size 72 (3 entries of 24 bytes), sh_link = 3
	write_u32_le(data, 128 + 128, 7);        // sh_name = ".symtab"
	write_u32_le(data, 128 + 128 + 4, 2);    // SHT_SYMTAB
	write_u64_le(data, 128 + 128 + 24, 600);
	write_u64_le(data, 128 + 128 + 32, 72);
	write_u32_le(data, 128 + 128 + 40, 3);   // sh_link = 3
	write_u64_le(data, 128 + 128 + 56, 24);  // sh_entsize = 24

	// Section 3: .shstrtab/.strtab at offset 500, size 53
	write_u32_le(data, 128 + 192, 15);       // sh_name = ".shstrtab"
	write_u32_le(data, 128 + 192 + 4, 3);    // SHT_STRTAB
	write_u64_le(data, 128 + 192 + 24, 500);
	write_u64_le(data, 128 + 192 + 32, 53);

	// Section 4: .rodata at offset 700, vaddr 0x402000, size 32
	write_u32_le(data, 128 + 256, 45);       // sh_name = ".rodata"
	write_u32_le(data, 128 + 256 + 4, 1);    // SHT_PROGBITS
	write_u64_le(data, 128 + 256 + 8, 2);    // SHF_ALLOC
	write_u64_le(data, 128 + 256 + 16, 0x402000);
	write_u64_le(data, 128 + 256 + 24, 700);
	write_u64_le(data, 128 + 256 + 32, 32);

	const std::string ro_str = "hello_rodata_str";
	for (size_t i = 0; i < ro_str.size(); ++i) {
		data[700 + i] = static_cast<uint8_t>(ro_str[i]);
	}
	data[700 + ro_str.size()] = 0;

	// Symbol 1 in .symtab (at 624): my_func (name offset 25), STT_FUNC (0x12), shndx=1, value=0x401000, size=16
	write_u32_le(data, 624, 25);
	data[624 + 4] = 0x12; // STB_GLOBAL | STT_FUNC
	write_u16_le(data, 624 + 6, 1);
	write_u64_le(data, 624 + 8, 0x401000);
	write_u64_le(data, 624 + 16, 16);

	// Symbol 2 in .symtab (at 648): callee_func (name offset 33), STT_FUNC (0x12), shndx=1, value=0x401010, size=16
	write_u32_le(data, 648, 33);
	data[648 + 4] = 0x12; // STB_GLOBAL | STT_FUNC
	write_u16_le(data, 648 + 6, 1);
	write_u64_le(data, 648 + 8, 0x401010);
	write_u64_le(data, 648 + 16, 16);

	// Instructions at 448 (0x1C0, vaddr 0x401000):
	// 448: 51 (push %rcx)
	// 449..451: 8D 45 FF (lea -0x1(%rbp), %eax)
	// 452..456: E8 07 00 00 00 (call 0x401010 <callee_func>)
	// 457..463: 48 8D 3D F0 0F 00 00 (lea 0xFF0(%rip), %rdi -> 0x402000 <str#1>)
	data[448] = 0x51;
	data[449] = 0x8D; data[450] = 0x45; data[451] = 0xFF;
	data[452] = 0xE8; data[453] = 0x07; data[454] = 0x00; data[455] = 0x00; data[456] = 0x00;
	data[457] = 0x48; data[458] = 0x8D; data[459] = 0x3D;
	data[460] = 0xF0; data[461] = 0x0F; data[462] = 0x00; data[463] = 0x00;
	data[464] = 0xC3;

	{
		std::ofstream out(test_file, std::ios::binary);
		out.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
	}

	tools::hexdump_args args;
	args.requested_path = test_file;
	args.safe_path = test_file;
	args.offset = 448;
	args.size = 16;

	tools::hexdump_tool tool(args);
	auto ctx = create_mock_context();
	std::string result = tool.execute(ctx);

#ifdef HAVE_ZYDIS
	// First occurrence of "lea" must exist, and second occurrence is the RIP-relative .rodata lea
	size_t first_lea = result.find("lea");
	assert(first_lea != std::string::npos);
	size_t second_lea = result.find("lea", first_lea + 3);
	assert(second_lea != std::string::npos);
	assert(result.find("lea", second_lea + 3) == std::string::npos);
	// Call target symbol resolution must annotate <callee_func>
	assert(result.find("<callee_func>") != std::string::npos);
	// .rodata string literal must be indexed as <str#1> inline and listed in the table below
	assert(result.find("<str#1>") != std::string::npos);
	assert(result.find("#### Referenced String Literals") != std::string::npos);
	assert(result.find("| `str#1` | `0x402000` | `\"hello_rodata_str\"` |") != std::string::npos);
	// Should not repeat verbose ELF Sec metadata on disassembled instruction rows
	assert(result.find("ELF Sec") == std::string::npos);
#endif
	std::filesystem::remove(test_file);
	std::cout << "test_hexdump_elf_code_dedup passed!" << std::endl;
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
		args.offset = 0;
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
		args.offset = 8;
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

	// Test 3: Excessively large offset cap rejection
	{
		tools::hexwrite_args args;
		args.requested_path = test_file;
		args.safe_path = test_file;
		args.offset = 1000000000; // 1 GB (exceeds 512 MB cap)
		args.hex_data = "aa bb";

		tools::hexwrite_tool tool(args);
		std::string result = tool.execute(ctx);
		assert(result.find("Error:") != std::string::npos);
		assert(result.find("exceeds maximum allowed limit") != std::string::npos);
	}

	// Test 4: Missing file offset_by_name error handling
	{
		tools::hexwrite_args args;
		args.requested_path = "non_existent_file_for_named_offset.bin";
		args.safe_path = "non_existent_file_for_named_offset.bin";
		args.offset = 0;
		args.hex_data = "aa bb";
		args.offset_by_name = ".text";

		tools::hexwrite_tool tool(args);
		std::string result = tool.execute(ctx);
		assert(result.find("Error:") != std::string::npos);
		assert(result.find("Could not open file") != std::string::npos);
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
		args.offset = 0;
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
		args.offset = 0;
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

void test_hexdump_validator()
{
	tools::hexdump_validator val;
	agentlib::tool_context ctx = create_mock_context();
	std::string err;

	// 1. Only path provided (size omitted) -> should validate and default size to 256
	nlohmann::json args1 = {{"path", "tests/testtar.tar"}};
	assert(val.validate_args(args1, ctx, err));
	auto tool1 = val.create_tool(args1);
	assert(tool1 != nullptr);

	// 2. Global alias: 'length' -> 'size'
	nlohmann::json args2 = {{"path", "tests/testtar.tar"}, {"length", 64}};
	assert(val.validate_args(args2, ctx, err));
	auto tool2 = val.create_tool(args2);
	assert(tool2 != nullptr);

	// 3. Global alias: 'bytes' -> 'size'
	nlohmann::json args3 = {{"path", "tests/testtar.tar"}, {"bytes", 32}};
	assert(val.validate_args(args3, ctx, err));

	// 4. Global alias: 'num_bytes' -> 'size'
	nlohmann::json args4 = {{"path", "tests/testtar.tar"}, {"num_bytes", 16}};
	assert(val.validate_args(args4, ctx, err));

	// 5. Size exceeding 4096 is clamped
	nlohmann::json args5 = {{"path", "tests/testtar.tar"}, {"size", 99999}};
	assert(val.validate_args(args5, ctx, err));

	// 6. Global alias: 'start_offset' -> 'offset' and 'filename' -> 'path'
	nlohmann::json args6 = {{"filename", "tests/testtar.tar"}, {"start_offset", 16}, {"length", 32}};
	assert(val.validate_args(args6, ctx, err));
	auto tool6 = val.create_tool(args6);
	assert(tool6 != nullptr);

	std::cout << "test_hexdump_validator passed!" << std::endl;
}

int main()
{
	test_watchdog::setup_watchdog(30);
	test_hexdump_tool();
	test_hexdump_elf_code_dedup();
	test_hexwrite_tool();
	test_named_offsets();
	test_hexdump_validator();
	std::cout << "All hexedit plugin unit tests passed!" << std::endl;
	return 0;
}
