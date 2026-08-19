// Tested source file: src/hex/elf.cpp, src/hex/png.cpp, src/hex/jpeg.cpp, src/hex/zip.cpp, src/hex/pdf.cpp, src/hex/tar.cpp
#include "test_watchdog.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>
#include "hex/hex_highlighter_registry.h"
#include "hex/elf.h"
#include "hex/png.h"
#include "hex/jpeg.h"
#include "hex/zip.h"
#include "hex/pdf.h"
#include "hex/tar.h"


// Helper to write values in little endian format to a vector
void write_u16_le(std::vector<uint8_t> &data, size_t offset, uint16_t val)
{
	data[offset] = val & 0xFF;
	data[offset + 1] = (val >> 8) & 0xFF;
}

void write_u32_le(std::vector<uint8_t> &data, size_t offset, uint32_t val)
{
	data[offset] = val & 0xFF;
	data[offset + 1] = (val >> 8) & 0xFF;
	data[offset + 2] = (val >> 16) & 0xFF;
	data[offset + 3] = (val >> 24) & 0xFF;
}

void write_u64_le(std::vector<uint8_t> &data, size_t offset, uint64_t val)
{
	write_u32_le(data, offset, val & 0xFFFFFFFF);
	write_u32_le(data, offset + 4, (val >> 32) & 0xFFFFFFFF);
}

void test_elf_highlighter()
{
	std::vector<uint8_t> data(1000, 0);

	// Construct a minimal 64-bit Little Endian ELF file

	// 1. Ehdr (0..63)
	data[0] = 0x7F;
	data[1] = 'E';
	data[2] = 'L';
	data[3] = 'F';
	data[4] = 2; // ELFCLASS64
	data[5] = 1; // ELFDATA2LSB
	data[6] = 1; // EV_CURRENT
	data[7] = 3; // OSABI Linux

	write_u16_le(data, 16, 2);	    // e_type = ET_EXEC
	write_u16_le(data, 18, 62);	    // e_machine = EM_X86_64
	write_u32_le(data, 20, 1);	    // e_version
	write_u64_le(data, 24, 0x00401000); // e_entry
	write_u64_le(data, 32, 64);	    // e_phoff = 64
	write_u64_le(data, 40, 128);	    // e_shoff = 128
	write_u16_le(data, 52, 64);	    // e_ehsize = 64
	write_u16_le(data, 54, 56);	    // e_phentsize = 56
	write_u16_le(data, 56, 1);	    // e_phnum = 1
	write_u16_le(data, 58, 64);	    // e_shentsize = 64
	write_u16_le(data, 60, 5);	    // e_shnum = 5 (Null, .text, .shstrtab, .symtab, .strtab)
	write_u16_le(data, 62, 2);	    // e_shstrndx = 2 (.shstrtab is index 2)

	// 2. Phdr (64..119)
	size_t ph_start = 64;
	write_u32_le(data, ph_start, 1);	     // p_type = PT_LOAD
	write_u32_le(data, ph_start + 4, 5);	     // p_flags = PF_R | PF_X
	write_u64_le(data, ph_start + 8, 450);	     // p_offset = 450 (.text starts at 450)
	write_u64_le(data, ph_start + 16, 0x401000); // p_vaddr
	write_u64_le(data, ph_start + 24, 0x401000); // p_paddr
	write_u64_le(data, ph_start + 32, 32);	     // p_filesz = 32
	write_u64_le(data, ph_start + 40, 32);	     // p_memsz = 32
	write_u64_le(data, ph_start + 48, 4096);     // p_align = 4KB

	// 3. String Table Data (starts at 500)
	// Indices:
	// 0: "\0"
	// 1: ".text._ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEC2EvQ26is_default_constructible\0"
	// 93: ".shstrtab\0"
	// 103: ".symtab\0"
	// 111: ".strtab\0"
	size_t strtab_start = 500;
	std::string text_name = ".text._ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEC2EvQ26is_default_constructible";
	std::string shstrtab_name = ".shstrtab";
	std::string symtab_name = ".symtab";
	std::string strtab_name = ".strtab";
	std::copy(text_name.begin(), text_name.end(), &data[strtab_start + 1]);
	std::copy(shstrtab_name.begin(), shstrtab_name.end(), &data[strtab_start + 93]);
	std::copy(symtab_name.begin(), symtab_name.end(), &data[strtab_start + 103]);
	std::copy(strtab_name.begin(), strtab_name.end(), &data[strtab_start + 111]);

	// 4. Shdr [0] - Null Section (128..191) is all zeroes

	// 5. Shdr [1] - .text Section (192..255)
	size_t sh1_start = 128 + 64;
	write_u32_le(data, sh1_start, 1);	      // sh_name = 1
	write_u32_le(data, sh1_start + 4, 1);	      // sh_type = SHT_PROGBITS
	write_u64_le(data, sh1_start + 8, 6);	      // sh_flags = SHF_ALLOC | SHF_EXECINSTR
	write_u64_le(data, sh1_start + 16, 0x401000); // sh_addr
	write_u64_le(data, sh1_start + 24, 450);      // sh_offset = 450
	write_u64_le(data, sh1_start + 32, 32);	      // sh_size = 32

	// 6. Shdr [2] - .shstrtab Section (256..319)
	size_t sh2_start = 128 + 128;
	write_u32_le(data, sh2_start, 93);	 // sh_name = 93
	write_u32_le(data, sh2_start + 4, 3);	 // sh_type = SHT_STRTAB
	write_u64_le(data, sh2_start + 8, 0);	 // sh_flags = 0
	write_u64_le(data, sh2_start + 16, 0);	 // sh_addr = 0
	write_u64_le(data, sh2_start + 24, 500); // sh_offset = 500
	write_u64_le(data, sh2_start + 32, 120); // sh_size = 120

	// 7. Shdr [3] - .symtab Section (320..383)
	size_t sh3_start = 128 + 192;
	write_u32_le(data, sh3_start, 103);	  // sh_name = 103
	write_u32_le(data, sh3_start + 4, 2);	  // sh_type = SHT_SYMTAB
	write_u64_le(data, sh3_start + 8, 0);	  // sh_flags = 0
	write_u64_le(data, sh3_start + 16, 0);	  // sh_addr = 0
	write_u64_le(data, sh3_start + 24, 650);  // sh_offset = 650
	write_u64_le(data, sh3_start + 32, 48);   // sh_size = 48 (2 entries)
	write_u32_le(data, sh3_start + 40, 4);    // sh_link = 4 (associated with .strtab)
	write_u32_le(data, sh3_start + 44, 1);    // sh_info = 1
	write_u64_le(data, sh3_start + 48, 8);    // sh_addralign = 8
	write_u64_le(data, sh3_start + 56, 24);   // sh_entsize = 24

	// 8. Shdr [4] - .strtab Section (384..447)
	size_t sh4_start = 128 + 256;
	write_u32_le(data, sh4_start, 111);	  // sh_name = 111
	write_u32_le(data, sh4_start + 4, 3);	  // sh_type = SHT_STRTAB
	write_u64_le(data, sh4_start + 8, 0);	  // sh_flags = 0
	write_u64_le(data, sh4_start + 16, 0);	  // sh_addr = 0
	write_u64_le(data, sh4_start + 24, 700);  // sh_offset = 700
	write_u64_le(data, sh4_start + 32, 10);   // sh_size = 10

	// Write .strtab data
	std::string func_sym_name = "my_func";
	std::copy(func_sym_name.begin(), func_sym_name.end(), &data[700 + 1]); // offset 701 is "my_func"

	// Write .symtab entry 1 (offset 674)
	size_t sym1_start = 650 + 24;
	write_u32_le(data, sym1_start, 1);		// st_name = 1 ("my_func")
	data[sym1_start + 4] = 0x12;			// st_info = STB_GLOBAL | STT_FUNC
	write_u16_le(data, sym1_start + 6, 1);		// st_shndx = 1
	write_u64_le(data, sym1_start + 8, 0x401000);	// st_value = 0x401000
	write_u64_le(data, sym1_start + 16, 32);	// st_size = 32

	// Write mock x86-64 instructions into .text at offset 450
	data[450] = 0x51; // push rcx
	data[451] = 0x8D; data[452] = 0x45; data[453] = 0xFF; // lea eax, [rbp-0x01]
	// mov eax, 0x00000321 (imm32: 21 03 00 00)
	data[454] = 0xB8; data[455] = 0x21; data[456] = 0x03; data[457] = 0x00; data[458] = 0x00;

	elf_hex_highlighter hl;
	assert(hl.can_handle(data) == true);

	bool success = hl.parse(data);
	assert(success == true);

	// Test Magic
	highlight_info inf = hl.get_info(data, 0);
	assert(inf.type == hex_semantic_type::magic);
	assert(inf.description.find("Magic") != std::string::npos);

	// Test e_ident class
	inf = hl.get_info(data, 4);
	assert(inf.type == hex_semantic_type::file_header);
	assert(inf.description.find("EI_CLASS") != std::string::npos);
	assert(inf.description.find("64-bit") != std::string::npos);

	// Test e_ident data
	inf = hl.get_info(data, 5);
	assert(inf.type == hex_semantic_type::file_header);
	assert(inf.description.find("EI_DATA") != std::string::npos);
	assert(inf.description.find("Little Endian") != std::string::npos);

	// Test e_type
	inf = hl.get_info(data, 16);
	assert(inf.type == hex_semantic_type::file_header);
	assert(inf.description.find("e_type") != std::string::npos);
	assert(inf.description.find("ET_EXEC") != std::string::npos);

	// Test Program Header Table range
	inf = hl.get_info(data, 64);
	assert(inf.type == hex_semantic_type::prog_header);
	assert(inf.description.find("p_type") != std::string::npos);
	assert(inf.description.find("PT_LOAD") != std::string::npos);

	// Test Section Header Table range
	inf = hl.get_info(data, 192);
	assert(inf.type == hex_semantic_type::sect_header);
	assert(inf.description.find("sh_name") != std::string::npos);
	assert(inf.description.find(".text.std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >::basic_string() [requires is_default_constructible]") != std::string::npos);

	// Test .text section body range: verify disassembly if HAVE_ZYDIS is defined
	inf = hl.get_info(data, 450);
	assert(inf.type == hex_semantic_type::code_section);
#ifdef HAVE_ZYDIS
	assert(inf.description.find("push") != std::string::npos);
	assert(inf.description.find("%rcx") != std::string::npos);
	assert(inf.description.find("in my_func") != std::string::npos);
	assert(inf.range_start == 450);
	assert(inf.range_size == 1);

	// Test address shortening for offset 454
	highlight_info inf_short = hl.get_info(data, 454);
	assert(inf_short.type == hex_semantic_type::code_section);
	assert(inf_short.description.find("mov") != std::string::npos);
	assert(inf_short.description.find("0x321") != std::string::npos);
	assert(inf_short.description.find("0x0000") == std::string::npos);
#else
	assert(inf.description.find("Sec \".text.std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >::basic_string() [requires is_default_constructible]\"") != std::string::npos);
#endif

	// Test .shstrtab section body range (mapped range: offset 500 to 619)
	inf = hl.get_info(data, 505);
	assert(inf.type == hex_semantic_type::symtab_section);
	assert(inf.description.find("Sec \".shstrtab\"") != std::string::npos);

	// Test auto-detect registry
	auto &reg = hex_highlighter_registry::get_instance();
	auto detected = reg.detect_highlighter(data);
	assert(detected != nullptr);
	assert(dynamic_cast<elf_hex_highlighter *>(detected.get()) != nullptr);

	// Test get_next_symbol_offset
	assert(hl.get_next_symbol_offset(0) == 450);
	assert(hl.get_next_symbol_offset(450) == 450);
}

void test_png_highlighter()
{
	std::vector<uint8_t> data(100, 0);

	// PNG Signature (8 bytes)
	data[0] = 0x89;
	data[1] = 0x50;
	data[2] = 0x4E;
	data[3] = 0x47;
	data[4] = 0x0D;
	data[5] = 0x0A;
	data[6] = 0x1A;
	data[7] = 0x0A;

	// IHDR chunk (starts at offset 8): length 13
	// length (4 bytes): 13 (Big Endian -> 0 0 0 13)
	data[8] = 0; data[9] = 0; data[10] = 0; data[11] = 13;
	// type (4 bytes): IHDR
	data[12] = 'I'; data[13] = 'H'; data[14] = 'D'; data[15] = 'R';
	// data (13 bytes):
	// width (4 bytes): 100 (0 0 0 100) -> data[16..19]
	data[19] = 100;
	// height (4 bytes): 200 (0 0 0 200) -> data[20..23]
	data[23] = 200;
	// bit depth (1 byte): 8
	data[24] = 8;
	// color type (1 byte): 2
	data[25] = 2;
	// compression, filter, interlace (1 byte each): 0
	data[26] = 0; data[27] = 0; data[28] = 0;
	// CRC (4 bytes): data[29..32]

	// IEND chunk (starts at offset 8 + 12 + 13 = 33): length 0
	// length (4 bytes): 0
	data[33] = 0; data[34] = 0; data[35] = 0; data[36] = 0;
	// type (4 bytes): IEND
	data[37] = 'I'; data[38] = 'E'; data[39] = 'N'; data[40] = 'D';
	// CRC (4 bytes): data[41..44]

	png_hex_highlighter hl;
	assert(hl.can_handle(data) == true);

	bool success = hl.parse(data);
	assert(success == true);

	// Test signature
	highlight_info inf = hl.get_info(data, 0);
	assert(inf.type == hex_semantic_type::magic);
	assert(inf.description.find("Signature") != std::string::npos);

	// Test IHDR length field (offset 8)
	inf = hl.get_info(data, 8);
	assert(inf.type == hex_semantic_type::file_header);
	assert(inf.description.find("Length = 13") != std::string::npos);

	// Test IHDR type field (offset 12)
	inf = hl.get_info(data, 12);
	assert(inf.type == hex_semantic_type::sect_header);
	assert(inf.description.find("Type") != std::string::npos);

	// Test IHDR data (offset 16)
	inf = hl.get_info(data, 16);
	assert(inf.type == hex_semantic_type::code_section);
	assert(inf.description.find("Width = 100") != std::string::npos);
	assert(inf.description.find("Height = 200") != std::string::npos);
	assert(inf.description.find("ColorType = 2") != std::string::npos);

	// Test get_next_symbol_offset
	assert(hl.get_next_symbol_offset(0) == 8);    // first chunk (IHDR) starts at 8
	assert(hl.get_next_symbol_offset(8) == 33);   // next chunk (IEND) starts at 33
	assert(hl.get_next_symbol_offset(33) == 33);  // no more chunks

	// Test get_structure_summary metadata integration
	std::string summary = hl.get_structure_summary();
	assert(summary.find("### PNG Structural Overview") != std::string::npos);
	assert(summary.find("MIME Type") != std::string::npos);
	assert(summary.find("image/png") != std::string::npos);
	assert(summary.find("100 x 200") != std::string::npos);
	assert(summary.find("RGB") != std::string::npos);

	// Test auto-detect registry
	auto &reg = hex_highlighter_registry::get_instance();
	auto detected = reg.detect_highlighter(data);
	assert(detected != nullptr);
	assert(dynamic_cast<png_hex_highlighter *>(detected.get()) != nullptr);
}

void test_jpeg_highlighter()
{
	std::vector<uint8_t> data(50, 0);

	// SOI marker (offset 0): FF D8
	data[0] = 0xFF;
	data[1] = 0xD8;

	// APP0 marker (offset 2): FF E0
	data[2] = 0xFF;
	data[3] = 0xE0;
	// length (2 bytes): 16 (Big Endian -> 0 16)
	data[4] = 0;
	data[5] = 16;
	// Identifier: "JFIF\0"
	data[6] = 'J'; data[7] = 'F'; data[8] = 'I'; data[9] = 'F'; data[10] = 0;
	// Major, minor version: 1.2
	data[11] = 1;
	data[12] = 2;
	// Units: 1 (Pixels/inch)
	data[13] = 1;
	// Xdensity: 72 (Big Endian -> 0 72)
	data[14] = 0;
	data[15] = 72;
	// Ydensity: 72 (Big Endian -> 0 72)
	data[16] = 0;
	data[17] = 72;
	// Thumbnail: 0x0
	data[18] = 0;
	data[19] = 0;

	// SOF0 marker (offset 20): FF C0
	data[20] = 0xFF;
	data[21] = 0xC0;
	// length (2 bytes): 8 (0 8)
	data[22] = 0;
	data[23] = 8;
	// precision: 8
	data[24] = 8;
	// height: 600 (Big Endian -> 2 88)
	data[25] = 2;
	data[26] = 88;
	// width: 800 (Big Endian -> 3 32)
	data[27] = 3;
	data[28] = 32;
	// components: 3
	data[29] = 3;

	// EOI marker (offset 30): FF D9
	data[30] = 0xFF;
	data[31] = 0xD9;

	jpeg_hex_highlighter hl;
	assert(hl.can_handle(data) == true);

	bool success = hl.parse(data);
	assert(success == true);

	// Test SOI
	highlight_info inf = hl.get_info(data, 0);
	assert(inf.type == hex_semantic_type::magic);
	assert(inf.description.find("Start of Image") != std::string::npos);

	// Test APP0 (offset 2)
	inf = hl.get_info(data, 2);
	assert(inf.type == hex_semantic_type::file_header);
	assert(inf.description.find("JFIF APP0") != std::string::npos);
	assert(inf.description.find("Ver 1.02") != std::string::npos);
	assert(inf.description.find("Pixels/inch") != std::string::npos);

	// Test SOF0 (offset 20)
	inf = hl.get_info(data, 20);
	assert(inf.type == hex_semantic_type::sect_header);
	assert(inf.description.find("SOF0 Header") != std::string::npos);
	assert(inf.description.find("Width = 800") != std::string::npos);
	assert(inf.description.find("Height = 600") != std::string::npos);

	// Test EOI
	inf = hl.get_info(data, 30);
	assert(inf.type == hex_semantic_type::magic);
	assert(inf.description.find("End of Image") != std::string::npos);

	// Test auto-detect registry
	auto &reg = hex_highlighter_registry::get_instance();
	auto detected = reg.detect_highlighter(data);
	assert(detected != nullptr);
	assert(dynamic_cast<jpeg_hex_highlighter *>(detected.get()) != nullptr);
}

void test_real_jpeg_file()
{
	std::string path = "docs/assets/logo.jpg";
	std::ifstream file(path, std::ios::binary);
	if (!file.is_open()) {
		path = "../docs/assets/logo.jpg";
		file.open(path, std::ios::binary);
	}
	if (!file.is_open()) {
		std::cout << "Skipping real JPEG file test (logo.jpg not found)" << std::endl;
		return;
	}

	file.seekg(0, std::ios::end);
	std::streamsize size = file.tellg();
	file.seekg(0, std::ios::beg);

	std::vector<uint8_t> data(size);
	file.read(reinterpret_cast<char *>(data.data()), size);
	file.close();

	jpeg_hex_highlighter hl;
	assert(hl.can_handle(data) == true);
	bool success = hl.parse(data);
	assert(success == true);

	// Test get_structure_summary metadata integration
	std::string summary = hl.get_structure_summary();
	assert(summary.find("### JPEG Structural Overview") != std::string::npos);
	assert(summary.find("MIME Type") != std::string::npos);
	assert(summary.find("image/jpeg") != std::string::npos);

	auto &reg = hex_highlighter_registry::get_instance();
	auto detected = reg.detect_highlighter(data);
	assert(detected != nullptr);
	assert(dynamic_cast<jpeg_hex_highlighter *>(detected.get()) != nullptr);
	std::cout << "Real JPEG file test passed!" << std::endl;
}

void test_zip_highlighter()
{
	std::string path = "tests/my-test-package.zip";
	std::ifstream file(path, std::ios::binary);
	if (!file.is_open()) {
		path = "../tests/my-test-package.zip";
		file.open(path, std::ios::binary);
	}
	if (!file.is_open()) {
		std::cout << "Skipping real ZIP file test (my-test-package.zip not found)" << std::endl;
		return;
	}

	file.seekg(0, std::ios::end);
	std::streamsize size = file.tellg();
	file.seekg(0, std::ios::beg);

	std::vector<uint8_t> data(size);
	file.read(reinterpret_cast<char *>(data.data()), size);
	file.close();

	zip_hex_highlighter hl;
	assert(hl.can_handle(data) == true);
	bool success = hl.parse(data);
	assert(success == true);

	// Verify local files are parsed
	const auto &files = hl.get_local_files();
	assert(!files.empty());

	// Verify we can find a file by name
	auto offset_opt = hl.get_offset_by_name("setup.py");
	assert(offset_opt.has_value());
	assert(*offset_opt > 0);

	// Verify get_info matches local file header
	size_t setup_offset = *offset_opt;
	highlight_info inf = hl.get_info(data, setup_offset);
	assert(inf.type == hex_semantic_type::file_header);
	assert(inf.description.find("setup.py") != std::string::npos);

	// Test auto-detect registry
	auto &reg = hex_highlighter_registry::get_instance();
	auto detected = reg.detect_highlighter(data);
	assert(detected != nullptr);
	assert(dynamic_cast<zip_hex_highlighter *>(detected.get()) != nullptr);
	std::cout << "Real ZIP file test passed!" << std::endl;
}

void test_pdf_highlighter()
{
	std::string path = "tests/shared-mime-info-spec.pdf";
	std::ifstream file(path, std::ios::binary);
	if (!file.is_open()) {
		path = "../tests/shared-mime-info-spec.pdf";
		file.open(path, std::ios::binary);
	}
	if (!file.is_open()) {
		std::cout << "Skipping real PDF file test (shared-mime-info-spec.pdf not found)" << std::endl;
		return;
	}

	file.seekg(0, std::ios::end);
	std::streamsize size = file.tellg();
	file.seekg(0, std::ios::beg);

	std::vector<uint8_t> data(size);
	file.read(reinterpret_cast<char *>(data.data()), size);
	file.close();

	pdf_hex_highlighter hl;
	assert(hl.can_handle(data) == true);
	bool success = hl.parse(data);
	assert(success == true);

	auto &reg = hex_highlighter_registry::get_instance();
	auto detected = reg.detect_highlighter(data);
	assert(detected != nullptr);
	assert(dynamic_cast<pdf_hex_highlighter *>(detected.get()) != nullptr);
	std::cout << "Real PDF file test passed!" << std::endl;
}

void test_tar_highlighter()
{
	std::vector<uint8_t> data(1536, 0); // 3 blocks of 512 bytes

	// Construct header for file "hello.txt", size 12 bytes octal "00000000014"
	std::string fname = "hello.txt";
	for (size_t i = 0; i < fname.length(); ++i) {
		data[i] = static_cast<uint8_t>(fname[i]);
	}

	// Size in octal at offset 124 (12 bytes)
	std::string size_oct = "00000000014";
	for (size_t i = 0; i < size_oct.length(); ++i) {
		data[124 + i] = static_cast<uint8_t>(size_oct[i]);
	}

	// ustar magic at offset 257 (5 bytes "ustar")
	data[257] = 'u';
	data[258] = 's';
	data[259] = 't';
	data[260] = 'a';
	data[261] = 'r';

	tar_hex_highlighter hl;
	assert(hl.can_handle(data) == true);
	bool success = hl.parse(data);
	assert(success == true);
	assert(hl.get_files().size() == 1);
	assert(hl.get_files()[0].filename == "hello.txt");
	assert(hl.get_files()[0].size == 12);

	highlight_info inf = hl.get_info(data, 10);
	assert(inf.type == hex_semantic_type::file_header);

	assert(hl.get_offset_by_name("hello.txt").has_value());
	assert(hl.get_next_symbol_offset(0) > 0);

	auto &reg = hex_highlighter_registry::get_instance();
	auto detected = reg.detect_highlighter(data);
	assert(detected != nullptr);
	assert(dynamic_cast<tar_hex_highlighter *>(detected.get()) != nullptr);
	std::cout << "TAR highlighter test passed!" << std::endl;
}

void test_malicious_corrupt_files()
{
	std::cout << "Starting robustness & malicious/corrupt file format tests..." << std::endl;

	// 1. ELF Malformed / Corrupted Payloads
	{
		elf_hex_highlighter elf_hl;

		// Truncated ELF inputs
		std::vector<uint8_t> elf_trunc1 = {0x7F, 'E', 'L', 'F'}; // Truncated (< 64 bytes)
		assert(!elf_hl.can_handle(elf_trunc1));

		std::vector<uint8_t> elf_trunc2(63, 0);
		elf_trunc2[0] = 0x7F; elf_trunc2[1] = 'E'; elf_trunc2[2] = 'L'; elf_trunc2[3] = 'F';
		assert(!elf_hl.can_handle(elf_trunc2));

		// Corrupt ELF class / endian
		std::vector<uint8_t> elf_bad_class(100, 0);
		elf_bad_class[0] = 0x7F; elf_bad_class[1] = 'E'; elf_bad_class[2] = 'L'; elf_bad_class[3] = 'F';
		elf_bad_class[4] = 0x99; // Invalid class
		elf_bad_class[5] = 0x01;
		assert(elf_hl.can_handle(elf_bad_class));
		assert(!elf_hl.parse(elf_bad_class));

		std::vector<uint8_t> elf_bad_data(100, 0);
		elf_bad_data[0] = 0x7F; elf_bad_data[1] = 'E'; elf_bad_data[2] = 'L'; elf_bad_data[3] = 'F';
		elf_bad_data[4] = 0x02;
		elf_bad_data[5] = 0x99; // Invalid data encoding
		assert(elf_hl.can_handle(elf_bad_data));
		assert(!elf_hl.parse(elf_bad_data));

		// Out of bounds / overflow section header offsets
		std::vector<uint8_t> elf_oof(100, 0);
		elf_oof[0] = 0x7F; elf_oof[1] = 'E'; elf_oof[2] = 'L'; elf_oof[3] = 'F';
		elf_oof[4] = 2; // ELFCLASS64
		elf_oof[5] = 1; // LSB
		write_u64_le(elf_oof, 40, 0xFFFFFFFFFFFFFFFFULL); // e_shoff out of bounds
		write_u16_le(elf_oof, 58, 64); // e_shentsize
		write_u16_le(elf_oof, 60, 100); // e_shnum
		assert(elf_hl.can_handle(elf_oof));
		elf_hl.parse(elf_oof);
		elf_hl.get_info(elf_oof, 10);
	}

	// 2. PNG Malformed / Corrupted Payloads
	{
		png_hex_highlighter png_hl;

		// Truncated PNG
		std::vector<uint8_t> png_trunc = {0x89, 'P', 'N', 'G'};
		assert(!png_hl.can_handle(png_trunc));

		// Chunk length overflow (0xFFFFFFFF)
		std::vector<uint8_t> png_wrap(100, 0);
		png_wrap[0] = 0x89; png_wrap[1] = 0x50; png_wrap[2] = 0x4E; png_wrap[3] = 0x47;
		png_wrap[4] = 0x0D; png_wrap[5] = 0x0A; png_wrap[6] = 0x1A; png_wrap[7] = 0x0A;
		png_wrap[8] = 0xFF; png_wrap[9] = 0xFF; png_wrap[10] = 0xFF; png_wrap[11] = 0xFF;
		png_wrap[12] = 'I'; png_wrap[13] = 'H'; png_wrap[14] = 'D'; png_wrap[15] = 'R';
		assert(png_hl.can_handle(png_wrap));
		png_hl.parse(png_wrap);
		png_hl.get_info(png_wrap, 5);
	}

	// 3. JPEG Malformed / Corrupted Payloads
	{
		jpeg_hex_highlighter jpeg_hl;

		// Truncated JPEG
		std::vector<uint8_t> jpeg_trunc = {0xFF, 0xD8};
		assert(!jpeg_hl.can_handle(jpeg_trunc));

		// Marker length overshoot
		std::vector<uint8_t> jpeg_bad(20, 0);
		jpeg_bad[0] = 0xFF; jpeg_bad[1] = 0xD8;
		jpeg_bad[2] = 0xFF; jpeg_bad[3] = 0xE0;
		jpeg_bad[4] = 0xFF; jpeg_bad[5] = 0xFF;
		assert(jpeg_hl.can_handle(jpeg_bad));
		jpeg_hl.parse(jpeg_bad);
		jpeg_hl.get_info(jpeg_bad, 3);

		// Consecutive 0xFF padding floods
		std::vector<uint8_t> jpeg_flood(100, 0xFF);
		jpeg_flood[0] = 0xFF; jpeg_flood[1] = 0xD8;
		assert(jpeg_hl.can_handle(jpeg_flood));
		jpeg_hl.parse(jpeg_flood);
	}

	// 4. ZIP Malformed / Corrupted Payloads
	{
		zip_hex_highlighter zip_hl;

		// Truncated ZIP
		std::vector<uint8_t> zip_trunc = {0x50, 0x4B};
		assert(!zip_hl.can_handle(zip_trunc));

		// ZIP EOCD with corrupt out of bounds CD offset
		std::vector<uint8_t> zip_bad(100, 0);
		zip_bad[0] = 0x50; zip_bad[1] = 0x4B; zip_bad[2] = 0x03; zip_bad[3] = 0x04;
		size_t eocd_pos = zip_bad.size() - 22;
		zip_bad[eocd_pos] = 0x50; zip_bad[eocd_pos + 1] = 0x4B;
		zip_bad[eocd_pos + 2] = 0x05; zip_bad[eocd_pos + 3] = 0x06;
		write_u32_le(zip_bad, eocd_pos + 16, 0xDEADBEEF);
		write_u32_le(zip_bad, eocd_pos + 12, 1000);
		assert(zip_hl.can_handle(zip_bad));
		zip_hl.parse(zip_bad);
		zip_hl.get_info(zip_bad, 0);
	}

	// 5. TAR Malformed / Corrupted Payloads
	{
		tar_hex_highlighter tar_hl;

		// Truncated TAR header (< 512 bytes)
		std::vector<uint8_t> tar_trunc(260, 'A');
		tar_trunc[257] = 'u'; tar_trunc[258] = 's'; tar_trunc[259] = 't';
		assert(!tar_hl.can_handle(tar_trunc));

		// Corrupt octal size with non-octal string
		std::vector<uint8_t> tar_bad(1024, 0);
		tar_bad[257] = 'u'; tar_bad[258] = 's'; tar_bad[259] = 't'; tar_bad[260] = 'a'; tar_bad[261] = 'r';
		std::string bad_oct = "INVALID_OCT!";
		for (size_t i = 0; i < bad_oct.length(); ++i) {
			tar_bad[124 + i] = static_cast<uint8_t>(bad_oct[i]);
		}
		assert(tar_hl.can_handle(tar_bad));
		tar_hl.parse(tar_bad);
		tar_hl.get_info(tar_bad, 0);
	}

	// 6. PDF Malformed / Corrupted Payloads
	{
		pdf_hex_highlighter pdf_hl;

		// Truncated PDF header
		std::vector<uint8_t> pdf_trunc = {'%', 'P', 'D'};
		assert(!pdf_hl.can_handle(pdf_trunc));

		// Unclosed obj / stream keywords
		std::string pdf_bad_str = "%PDF-1.4\n1 0 obj\n<< /Length 99999 >>\nstream\ngarbage bytes...\n";
		std::vector<uint8_t> pdf_bad(pdf_bad_str.begin(), pdf_bad_str.end());
		assert(pdf_hl.can_handle(pdf_bad));
		pdf_hl.parse(pdf_bad);
		pdf_hl.get_info(pdf_bad, 10);
	}

	std::cout << "All robustness & malicious/corrupt file format tests passed cleanly!" << std::endl;
}

int main()
{
	test_watchdog::setup_watchdog(30);
	test_elf_highlighter();
	test_png_highlighter();
	test_jpeg_highlighter();
	test_real_jpeg_file();
	test_zip_highlighter();
	test_pdf_highlighter();
	test_tar_highlighter();
	test_malicious_corrupt_files();
	std::cout << "All hex syntax highlighter tests passed!" << std::endl;
	return 0;
}

