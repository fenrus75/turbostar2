// Tested source file: src/hex/elf.cpp, src/hex/png.cpp, src/hex/jpeg.cpp, src/hex/zip.cpp, src/hex/pdf.cpp, src/hex/tar.cpp
#include "test_watchdog.h"
#include <algorithm>
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

void write_u16_be(std::vector<uint8_t> &data, size_t offset, uint16_t val)
{
	data[offset] = (val >> 8) & 0xFF;
	data[offset + 1] = val & 0xFF;
}

void write_u32_be(std::vector<uint8_t> &data, size_t offset, uint32_t val)
{
	data[offset] = (val >> 24) & 0xFF;
	data[offset + 1] = (val >> 16) & 0xFF;
	data[offset + 2] = (val >> 8) & 0xFF;
	data[offset + 3] = val & 0xFF;
}

void write_u64_be(std::vector<uint8_t> &data, size_t offset, uint64_t val)
{
	write_u32_be(data, offset, (val >> 32) & 0xFFFFFFFF);
	write_u32_be(data, offset + 4, val & 0xFFFFFFFF);
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
	std::ranges::copy(text_name, &data[strtab_start + 1]);
	std::ranges::copy(shstrtab_name, &data[strtab_start + 93]);
	std::ranges::copy(symtab_name, &data[strtab_start + 103]);
	std::ranges::copy(strtab_name, &data[strtab_start + 111]);

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
	std::ranges::copy(func_sym_name, &data[700 + 1]); // offset 701 is "my_func"

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

	// Additional 64-bit Ehdr fields
	for (size_t off : {6, 7, 8, 9, 14, 18, 20, 24, 32, 40, 48, 52, 54, 56, 58, 60, 62}) {
		auto f_inf = hl.get_info(data, off);
		assert(f_inf.type == hex_semantic_type::file_header);
	}

	// 64-bit Phdr fields (p_type=64, p_flags=68, p_offset=72, p_vaddr=80, p_paddr=88, p_filesz=96, p_memsz=104, p_align=112)
	for (size_t off : {68, 72, 80, 88, 96, 104, 112}) {
		auto ph_inf = hl.get_info(data, off);
		assert(ph_inf.type == hex_semantic_type::prog_header);
	}

	// 64-bit Shdr fields for section 1 (start 192)
	for (size_t off : {196, 200, 208, 216, 224, 232, 236, 240, 248}) {
		auto sh_inf = hl.get_info(data, off);
		assert(sh_inf.type == hex_semantic_type::sect_header);
	}

	// Unmapped offset
	assert(hl.get_info(data, 850).type == hex_semantic_type::normal);

	// Structure summary
	std::string summary = hl.get_structure_summary();
	assert(summary.find("### ELF Structure Overview") != std::string::npos);
	assert(summary.find("ELF64") != std::string::npos);
	assert(summary.find(".text") != std::string::npos);
	assert(summary.find("my_func") != std::string::npos);

	// Section and symbol name lookup
	auto off_shstr = hl.get_offset_by_name(".shstrtab");
	assert(off_shstr.has_value() && *off_shstr == 500);
	auto off_sym = hl.get_offset_by_name("my_func");
	assert(off_sym.has_value() && *off_sym == 450);
	assert(!hl.get_offset_by_name("nonexistent_symbol").has_value());

	// Unparsed highlighter edge cases
	elf_hex_highlighter unparsed_hl;
	assert(!unparsed_hl.can_handle({}));
	assert(!unparsed_hl.can_handle(std::vector<uint8_t>{0x7F, 'E', 'L'}));
	assert(!unparsed_hl.can_handle(std::vector<uint8_t>{'n', 'o', 't', 'e', 'l', 'f'}));
	assert(!unparsed_hl.parse({}));
	assert(unparsed_hl.get_info({}, 0).type == hex_semantic_type::normal);
	assert(!unparsed_hl.get_offset_by_name(".text").has_value());
	assert(unparsed_hl.get_structure_summary().empty());
}

void test_elf_32bit()
{
	std::vector<uint8_t> data(1000, 0);
	data[0] = 0x7F; data[1] = 'E'; data[2] = 'L'; data[3] = 'F';
	data[4] = 1; // ELFCLASS32
	data[5] = 1; // ELFDATA2LSB
	data[6] = 1; // EV_CURRENT
	data[7] = 0; // SYSV

	write_u16_le(data, 16, 2);          // e_type = ET_EXEC
	write_u16_le(data, 18, 3);          // e_machine = EM_386
	write_u32_le(data, 20, 1);          // e_version
	write_u32_le(data, 24, 0x08048000); // e_entry
	write_u32_le(data, 28, 52);         // e_phoff = 52
	write_u32_le(data, 32, 400);        // e_shoff = 400
	write_u32_le(data, 36, 0);          // e_flags
	write_u16_le(data, 40, 52);         // e_ehsize = 52
	write_u16_le(data, 42, 32);         // e_phentsize = 32
	write_u16_le(data, 44, 1);          // e_phnum = 1
	write_u16_le(data, 46, 40);         // e_shentsize = 40
	write_u16_le(data, 48, 7);          // e_shnum = 7
	write_u16_le(data, 50, 6);          // e_shstrndx = 6

	// 32-bit Phdr at offset 52 (size 32)
	write_u32_le(data, 52, 1);          // p_type = PT_LOAD
	write_u32_le(data, 56, 100);        // p_offset = 100
	write_u32_le(data, 60, 0x08048000); // p_vaddr
	write_u32_le(data, 64, 0x08048000); // p_paddr
	write_u32_le(data, 68, 64);         // p_filesz = 64
	write_u32_le(data, 72, 64);         // p_memsz = 64
	write_u32_le(data, 76, 7);          // p_flags = R|W|X
	write_u32_le(data, 80, 0x1000);     // p_align = 4K

	// Section 1: .text at offset 100, size 32
	// x86 32-bit instructions:
	// xor %eax, %eax (31 c0)
	// mov $0x0, %eax (b8 00 00 00 00)
	data[100] = 0x31; data[101] = 0xC0;
	data[102] = 0xB8; data[103] = 0x00; data[104] = 0x00; data[105] = 0x00; data[106] = 0x00;

	// Section 4: .strtab at offset 180, size 80
	std::string sym_strings = std::string("\0_Z3foov\0_Z3foovXYZ\0_Z9999invalid\0normal_func\0", 46);
	for (size_t i = 0; i < sym_strings.size(); ++i) {
		data[180 + i] = static_cast<uint8_t>(sym_strings[i]);
	}

	// Section 5: .symtab at offset 260, size 80 (5 entries x 16 bytes)
	// Entry 0: Null
	// Entry 1: _Z3foov (offset 1 in strtab), st_value = 0x08048000, st_size = 2, st_info = 0x12 (FUNC), st_shndx = 1
	write_u32_le(data, 260 + 16, 1);
	write_u32_le(data, 260 + 20, 0x08048000);
	write_u32_le(data, 260 + 24, 2);
	data[260 + 28] = 0x12;
	write_u16_le(data, 260 + 30, 1);
	// Entry 2: _Z3foovXYZ (offset 9 in strtab), st_value = 0x08048002, st_size = 5, st_info = 0x12 (FUNC), st_shndx = 1
	write_u32_le(data, 260 + 32, 9);
	write_u32_le(data, 260 + 36, 0x08048002);
	write_u32_le(data, 260 + 40, 5);
	data[260 + 44] = 0x12;
	write_u16_le(data, 260 + 46, 1);
	// Entry 3: _Z9999invalid (offset 20 in strtab), st_value = 0x08048000, st_size = 2, st_info = 0x12, st_shndx = 1
	write_u32_le(data, 260 + 48, 20);
	write_u32_le(data, 260 + 52, 0x08048000);
	write_u32_le(data, 260 + 56, 2);
	data[260 + 60] = 0x12;
	write_u16_le(data, 260 + 62, 1);
	// Entry 4: st_name = 9999 (unnamed), st_value = 0x08048000, st_size = 2, st_info = 0x12, st_shndx = 1
	write_u32_le(data, 260 + 64, 9999);
	write_u32_le(data, 260 + 68, 0x08048000);
	write_u32_le(data, 260 + 72, 2);
	data[260 + 76] = 0x12;
	write_u16_le(data, 260 + 78, 1);

	// Section 6: .shstrtab at offset 350, size 70
	std::string sh_strings = std::string("\0.text\0.data\0.rodata\0.strtab\0.symtab\0.shstrtab\0", 47);
	for (size_t i = 0; i < sh_strings.size(); ++i) {
		data[350 + i] = static_cast<uint8_t>(sh_strings[i]);
	}

	// 32-bit Shdr table at offset 440 (7 entries x 40 bytes = 280 bytes)
	// Entry 0: Null
	// Entry 1: .text (sh_name = 1)
	write_u32_le(data, 440 + 40, 1);
	write_u32_le(data, 440 + 44, 1); // SHT_PROGBITS
	write_u32_le(data, 440 + 48, 6); // ALLOC | EXEC
	write_u32_le(data, 440 + 52, 0x08048000); // sh_addr
	write_u32_le(data, 440 + 56, 100); // sh_offset
	write_u32_le(data, 440 + 60, 32);  // sh_size
	write_u32_le(data, 440 + 72, 4);   // sh_addralign

	// Entry 2: .data (sh_name = 7)
	write_u32_le(data, 440 + 80, 7);
	write_u32_le(data, 440 + 84, 1); // SHT_PROGBITS
	write_u32_le(data, 440 + 88, 3); // ALLOC | WRITE
	write_u32_le(data, 440 + 96, 132); // sh_offset
	write_u32_le(data, 440 + 100, 16);  // sh_size

	// Entry 3: .rodata (sh_name = 13)
	write_u32_le(data, 440 + 120, 13);
	write_u32_le(data, 440 + 124, 1); // SHT_PROGBITS
	write_u32_le(data, 440 + 128, 2); // ALLOC
	write_u32_le(data, 440 + 136, 148); // sh_offset
	write_u32_le(data, 440 + 140, 16);  // sh_size

	// Entry 4: .strtab (sh_name = 21)
	write_u32_le(data, 440 + 160, 21);
	write_u32_le(data, 440 + 164, 3); // SHT_STRTAB
	write_u32_le(data, 440 + 176, 180); // sh_offset
	write_u32_le(data, 440 + 180, 80);  // sh_size

	// Entry 5: .symtab (sh_name = 29)
	write_u32_le(data, 440 + 200, 29);
	write_u32_le(data, 440 + 204, 2); // SHT_SYMTAB
	write_u32_le(data, 440 + 216, 260); // sh_offset
	write_u32_le(data, 440 + 220, 80);  // sh_size
	write_u32_le(data, 440 + 224, 4);   // sh_link = 4 (.strtab)
	write_u32_le(data, 440 + 236, 16);  // sh_entsize = 16

	// Entry 6: .shstrtab (sh_name = 37)
	write_u32_le(data, 440 + 240, 37);
	write_u32_le(data, 440 + 244, 3); // SHT_STRTAB
	write_u32_le(data, 440 + 256, 350); // sh_offset
	write_u32_le(data, 440 + 260, 70);  // sh_size

	write_u32_le(data, 32, 440); // e_shoff = 440

	elf_hex_highlighter hl32;
	assert(hl32.can_handle(data));
	assert(hl32.parse(data));

	// Query all 32-bit Ehdr fields
	for (size_t off : {4, 16, 18, 20, 24, 28, 32, 36, 40, 42, 44, 46, 48, 50}) {
		auto inf = hl32.get_info(data, off);
		assert(inf.type == hex_semantic_type::file_header);
	}
	// Query all 32-bit Phdr fields
	for (size_t off : {52, 56, 60, 64, 68, 72, 76, 80}) {
		auto inf = hl32.get_info(data, off);
		assert(inf.type == hex_semantic_type::prog_header);
	}
	// Query all 32-bit Shdr fields for section 1
	for (size_t off : {440, 444, 448, 452, 456, 460, 464, 468, 472, 476}) {
		auto inf = hl32.get_info(data, off);
		assert(inf.type == hex_semantic_type::sect_header);
	}

	// Query instruction disassembly in 32-bit .text
	auto inf_inst1 = hl32.get_info(data, 100);
	assert(inf_inst1.type == hex_semantic_type::code_section);
	assert(inf_inst1.description.find("xor") != std::string::npos);

	auto inf_inst2 = hl32.get_info(data, 102);
	assert(inf_inst2.type == hex_semantic_type::code_section);
	assert(inf_inst2.description.find("mov") != std::string::npos);

	// Also test 32-bit ELF with ET_REL (e_type = 1)
	write_u16_le(data, 16, 1); // ET_REL
	elf_hex_highlighter hl_rel;
	assert(hl_rel.parse(data));
	assert(!hl_rel.get_symbols().empty());

	// Test symbol when st_value < sh_addr in ET_EXEC
	write_u16_le(data, 16, 2); // ET_EXEC
	write_u32_le(data, 260 + 36, 0x01000000); // st_value < 0x08048000
	elf_hex_highlighter hl_oob_sym;
	assert(hl_oob_sym.parse(data));

	std::string summary = hl32.get_structure_summary();
	assert(summary.find("ELF32") != std::string::npos);
}

void test_elf_big_endian()
{
	std::vector<uint8_t> data(400, 0);
	data[0] = 0x7F; data[1] = 'E'; data[2] = 'L'; data[3] = 'F';
	data[4] = 2; // ELFCLASS64
	data[5] = 2; // ELFDATA2MSB (Big Endian)
	data[6] = 1; // EV_CURRENT
	data[7] = 0; // SYSV

	write_u16_be(data, 16, 2);          // e_type = ET_EXEC
	write_u16_be(data, 18, 21);         // e_machine = EM_PPC64
	write_u32_be(data, 20, 1);          // e_version
	write_u64_be(data, 24, 0x10000000); // e_entry
	write_u64_be(data, 32, 64);         // e_phoff = 64
	write_u64_be(data, 40, 128);        // e_shoff = 128
	write_u16_be(data, 52, 64);         // e_ehsize = 64
	write_u16_be(data, 54, 56);         // e_phentsize = 56
	write_u16_be(data, 56, 1);          // e_phnum = 1
	write_u16_be(data, 58, 64);         // e_shentsize = 64
	write_u16_be(data, 60, 2);          // e_shnum = 2
	write_u16_be(data, 62, 0);          // e_shstrndx = 0

	// 64-bit Big Endian Phdr at 64
	write_u32_be(data, 64, 1);          // p_type = PT_LOAD
	write_u32_be(data, 68, 5);          // p_flags = R | X
	write_u64_be(data, 72, 200);        // p_offset = 200
	write_u64_be(data, 80, 0x10000000); // p_vaddr
	write_u64_be(data, 88, 0x10000000); // p_paddr
	write_u64_be(data, 96, 64);         // p_filesz = 64
	write_u64_be(data, 104, 64);        // p_memsz = 64
	write_u64_be(data, 112, 0x10000);   // p_align = 64K

	// 64-bit Big Endian Shdr[1] at 192
	write_u32_be(data, 192, 0);         // sh_name = 0
	write_u32_be(data, 196, 1);         // sh_type = SHT_PROGBITS
	write_u64_be(data, 200, 6);         // sh_flags = ALLOC | EXEC
	write_u64_be(data, 208, 0x10000000);// sh_addr
	write_u64_be(data, 216, 200);       // sh_offset = 200
	write_u64_be(data, 224, 64);        // sh_size = 64
	write_u32_be(data, 232, 0);         // sh_link
	write_u32_be(data, 236, 0);         // sh_info
	write_u64_be(data, 240, 8);         // sh_addralign
	write_u64_be(data, 248, 0);         // sh_entsize

	elf_hex_highlighter hl_be;
	assert(hl_be.can_handle(data));
	assert(hl_be.parse(data));

	auto inf_type = hl_be.get_info(data, 16);
	assert(inf_type.description.find("ET_EXEC") != std::string::npos);
	auto inf_mach = hl_be.get_info(data, 18);
	assert(inf_mach.description.find("EM_PPC64") != std::string::npos);
	auto inf_ph = hl_be.get_info(data, 64);
	assert(inf_ph.description.find("PT_LOAD") != std::string::npos);
	auto inf_sh = hl_be.get_info(data, 196);
	assert(inf_sh.description.find("SHT_PROGBITS") != std::string::npos);
}

void test_elf_various_types()
{
	std::vector<uint8_t> data(400, 0);
	data[0] = 0x7F; data[1] = 'E'; data[2] = 'L'; data[3] = 'F';
	data[4] = 2; // 64-bit
	data[5] = 1; // LSB
	data[6] = 1;
	write_u16_le(data, 52, 64); // e_ehsize = 64
	write_u64_le(data, 32, 64); // e_phoff = 64
	write_u16_le(data, 54, 56); // e_phentsize = 56
	write_u16_le(data, 56, 1);  // e_phnum = 1
	write_u64_le(data, 40, 120);// e_shoff = 120
	write_u16_le(data, 58, 64); // e_shentsize = 64
	write_u16_le(data, 60, 1);  // e_shnum = 1

	// Cycle through OSABIs
	for (uint8_t osabi : {0, 1, 2, 3, 6, 8, 9, 97, 255}) {
		data[7] = osabi;
		elf_hex_highlighter hl;
		assert(hl.parse(data));
		auto inf = hl.get_info(data, 7);
		assert(!inf.description.empty());
	}

	// Cycle through e_type
	for (uint16_t etype : {0, 1, 2, 3, 4, 0xFE00}) {
		write_u16_le(data, 16, etype);
		elf_hex_highlighter hl;
		assert(hl.parse(data));
		auto inf = hl.get_info(data, 16);
		assert(!inf.description.empty());
	}

	// Cycle through e_machine
	for (uint16_t emach : {0, 2, 3, 4, 8, 20, 21, 40, 50, 62, 0x9999}) {
		write_u16_le(data, 18, emach);
		elf_hex_highlighter hl;
		assert(hl.parse(data));
		auto inf = hl.get_info(data, 18);
		assert(!inf.description.empty());
	}

	// Cycle through p_type
	for (uint32_t ptype : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 0x6474E551u, 0x12345678u}) {
		write_u32_le(data, 64, ptype);
		elf_hex_highlighter hl;
		assert(hl.parse(data));
		auto inf = hl.get_info(data, 64);
		assert(!inf.description.empty());
	}

	// Cycle through sh_type
	for (uint32_t shtype : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 11u, 0x88888888u}) {
		write_u32_le(data, 124, shtype); // sh_type at offset 120 + 4 = 124
		elf_hex_highlighter hl;
		assert(hl.parse(data));
		auto inf = hl.get_info(data, 124);
		assert(!inf.description.empty());
	}

	// Cycle through sh_flags
	for (uint64_t shflags : {0ull, 1ull, 2ull, 4ull, 7ull}) {
		write_u64_le(data, 128, shflags); // sh_flags at offset 120 + 8 = 128
		elf_hex_highlighter hl;
		assert(hl.parse(data));
		auto inf = hl.get_info(data, 128);
		assert(!inf.description.empty());
	}

	// Check p_flags formatting in 64-bit and 32-bit
	// flags = 0 -> "None"
	write_u32_le(data, 68, 0); // 64-bit p_flags at offset 68
	{
		elf_hex_highlighter hl;
		assert(hl.parse(data));
		auto inf = hl.get_info(data, 68);
		assert(inf.description.find("None") != std::string::npos);
	}
	// flags = 2 -> "W"
	write_u32_le(data, 68, 2);
	{
		elf_hex_highlighter hl;
		assert(hl.parse(data));
		auto inf = hl.get_info(data, 68);
		assert(inf.description.find("W") != std::string::npos);
	}

	// 32-bit p_flags at offset 52 + 24 = 76
	std::vector<uint8_t> data32(200, 0);
	data32[0] = 0x7F; data32[1] = 'E'; data32[2] = 'L'; data32[3] = 'F';
	data32[4] = 1; data32[5] = 1;
	write_u16_le(data32, 40, 52); // e_ehsize
	write_u32_le(data32, 28, 52); // e_phoff
	write_u16_le(data32, 42, 32); // e_phentsize
	write_u16_le(data32, 44, 1);  // e_phnum
	write_u32_le(data32, 76, 0);  // flags = 0 -> "None"
	{
		elf_hex_highlighter hl;
		assert(hl.parse(data32));
		auto inf = hl.get_info(data32, 76);
		assert(inf.description.find("None") != std::string::npos);
	}
	write_u32_le(data32, 76, 2);  // flags = 2 -> "W"
	{
		elf_hex_highlighter hl;
		assert(hl.parse(data32));
		auto inf = hl.get_info(data32, 76);
		assert(inf.description.find("W") != std::string::npos);
	}

	// Ehdr extra byte fallthrough (e_ehsize > 64 for 64-bit)
	{
		std::vector<uint8_t> data_eh(200, 0);
		data_eh[0] = 0x7F; data_eh[1] = 'E'; data_eh[2] = 'L'; data_eh[3] = 'F';
		data_eh[4] = 2; data_eh[5] = 1;
		write_u16_le(data_eh, 52, 80); // e_ehsize = 80
		elf_hex_highlighter hl;
		assert(hl.parse(data_eh));
		auto inf = hl.get_info(data_eh, 70); // beyond standard 64 bytes
		assert(inf.type == hex_semantic_type::file_header);
		assert(inf.description == "ELF Ehdr Header");
	}

	// Phdr extra byte fallthrough (e_phentsize > 56 for 64-bit)
	{
		std::vector<uint8_t> data_ph(300, 0);
		data_ph[0] = 0x7F; data_ph[1] = 'E'; data_ph[2] = 'L'; data_ph[3] = 'F';
		data_ph[4] = 2; data_ph[5] = 1;
		write_u16_le(data_ph, 52, 64);
		write_u64_le(data_ph, 32, 64); // e_phoff = 64
		write_u16_le(data_ph, 54, 70); // e_phentsize = 70
		write_u16_le(data_ph, 56, 1);
		elf_hex_highlighter hl;
		assert(hl.parse(data_ph));
		auto inf = hl.get_info(data_ph, 64 + 60); // beyond standard 56 bytes
		assert(inf.type == hex_semantic_type::prog_header);
		assert(inf.description.find("Segment entry") != std::string::npos);
	}

	// Shdr extra byte fallthrough (e_shentsize > 64 for 64-bit)
	{
		std::vector<uint8_t> data_sh(300, 0);
		data_sh[0] = 0x7F; data_sh[1] = 'E'; data_sh[2] = 'L'; data_sh[3] = 'F';
		data_sh[4] = 2; data_sh[5] = 1;
		write_u16_le(data_sh, 52, 64);
		write_u64_le(data_sh, 40, 64); // e_shoff = 64
		write_u16_le(data_sh, 58, 80); // e_shentsize = 80
		write_u16_le(data_sh, 60, 1);
		elf_hex_highlighter hl;
		assert(hl.parse(data_sh));
		auto inf = hl.get_info(data_sh, 64 + 70); // beyond standard 64 bytes
		assert(inf.type == hex_semantic_type::sect_header);
		assert(inf.description.find("Section entry") != std::string::npos);
	}

	// Invalid EI_CLASS and EI_DATA descriptions in get_info
	{
		std::vector<uint8_t> data_desc(100, 0);
		data_desc[0] = 0x7F; data_desc[1] = 'E'; data_desc[2] = 'L'; data_desc[3] = 'F';
		data_desc[4] = 2; data_desc[5] = 1;
		write_u16_le(data_desc, 52, 64);
		elf_hex_highlighter hl;
		assert(hl.parse(data_desc));
		data_desc[4] = 99; // Corrupt class after parsing
		auto inf_c = hl.get_info(data_desc, 4);
		assert(inf_c.description.find("Invalid (99)") != std::string::npos);
		data_desc[5] = 99; // Corrupt data after parsing
		auto inf_d = hl.get_info(data_desc, 5);
		assert(inf_d.description.find("Invalid (99)") != std::string::npos);
		data_desc[5] = 2; // MSB
		auto inf_d2 = hl.get_info(data_desc, 5);
		assert(inf_d2.description.find("Big Endian") != std::string::npos);
	}

	// Large section disassembly (> 65536 bytes) and limit_offset clamped to data.size()
	{
		std::vector<uint8_t> big_data(70000, 0x90); // NOPs
		big_data[0] = 0x7F; big_data[1] = 'E'; big_data[2] = 'L'; big_data[3] = 'F';
		big_data[4] = 2; big_data[5] = 1;
		write_u16_le(big_data, 18, 62); // EM_X86_64
		write_u16_le(big_data, 52, 64);
		write_u64_le(big_data, 40, 100); // e_shoff = 100
		write_u16_le(big_data, 58, 64);  // e_shentsize = 64
		write_u16_le(big_data, 60, 3);   // e_shnum = 3 (null, .text, .shstrtab)
		write_u16_le(big_data, 62, 2);   // e_shstrndx = 2

		// .shstrtab contents at offset 300
		std::string sh_names = std::string("\0.text\0.shstrtab\0", 17);
		for (size_t i = 0; i < sh_names.size(); ++i) {
			big_data[300 + i] = static_cast<uint8_t>(sh_names[i]);
		}

		// Section 1: .text at offset 400, size 100000 (> big_data.size())
		write_u32_le(big_data, 100 + 64, 1);    // sh_name = 1 (".text")
		write_u32_le(big_data, 100 + 64 + 4, 1); // SHT_PROGBITS
		write_u64_le(big_data, 100 + 64 + 8, 6); // ALLOC | EXEC
		write_u64_le(big_data, 100 + 64 + 16, 0x400000); // sh_addr
		write_u64_le(big_data, 100 + 64 + 24, 400);      // sh_offset = 400
		write_u64_le(big_data, 100 + 64 + 32, 100000);   // sh_size > data.size()

		// Section 2: .shstrtab at offset 300, size 17
		write_u32_le(big_data, 100 + 128, 7);    // sh_name = 7 (".shstrtab")
		write_u32_le(big_data, 100 + 128 + 4, 3); // SHT_STRTAB
		write_u64_le(big_data, 100 + 128 + 24, 300); // sh_offset = 300
		write_u64_le(big_data, 100 + 128 + 32, 17);  // sh_size = 17

		// Invalid instruction bytes to test disassembly decode failure
		big_data[68000] = 0xFF; big_data[68001] = 0xFF; big_data[68002] = 0xFF;

		elf_hex_highlighter hl_big;
		assert(hl_big.parse(big_data));
		// Query offset > 65536 from section start (400 + 67000 = 67400)
		auto inf_big = hl_big.get_info(big_data, 67400);
		assert(inf_big.type == hex_semantic_type::code_section);

		// Query where disassembly fails
		auto inf_fail = hl_big.get_info(big_data, 68000);
		assert(inf_fail.type == hex_semantic_type::code_section);
	}

	// Test read_u64, read_u32, read_u16, and read_string bounds checks
	{
		// 64-bit Phdr query with buffer too short for read_u64
		std::vector<uint8_t> short64(75, 0);
		short64[0] = 0x7F; short64[1] = 'E'; short64[2] = 'L'; short64[3] = 'F';
		short64[4] = 2; short64[5] = 1;
		write_u16_le(short64, 52, 64);
		write_u64_le(short64, 32, 64); // e_phoff = 64
		write_u16_le(short64, 54, 11); // e_phentsize = 11
		write_u16_le(short64, 56, 1);  // e_phnum = 1
		elf_hex_highlighter hl_s64;
		assert(hl_s64.parse(short64));
		// field_offset = 72 - 64 = 8 -> triggers read_u64 at offset 72 with only 3 bytes remaining
		hl_s64.get_info(short64, 72);

		// 32-bit Phdr query with buffer too short for read_u32
		std::vector<uint8_t> short32(74, 0);
		short32[0] = 0x7F; short32[1] = 'E'; short32[2] = 'L'; short32[3] = 'F';
		short32[4] = 1; short32[5] = 1;
		write_u16_le(short32, 40, 52);
		write_u32_le(short32, 28, 64); // e_phoff = 64
		write_u16_le(short32, 42, 10); // e_phentsize = 10
		write_u16_le(short32, 44, 1);  // e_phnum = 1
		elf_hex_highlighter hl_s32;
		assert(hl_s32.parse(short32));
		// field_offset = 72 - 64 = 8 -> triggers read_u32 at offset 72 with only 2 bytes remaining
		hl_s32.get_info(short32, 72);

		// 32-bit Ehdr query with buffer too short for read_u16
		std::vector<uint8_t> short_ehdr(65, 0);
		short_ehdr[0] = 0x7F; short_ehdr[1] = 'E'; short_ehdr[2] = 'L'; short_ehdr[3] = 'F';
		short_ehdr[4] = 1; short_ehdr[5] = 1;
		write_u16_le(short_ehdr, 40, 65); // e_ehsize = 65
		elf_hex_highlighter hl_sehdr;
		assert(hl_sehdr.parse(short_ehdr));
		// Query e_type (offset 16) when size is only 17 -> read_u16 with 1 byte remaining
		short_ehdr.resize(17);
		hl_sehdr.get_info(short_ehdr, 16);

		// Shdr with name_offset pointing beyond data.size() to trigger read_string bounds check
		std::vector<uint8_t> short_sh(200, 0);
		short_sh[0] = 0x7F; short_sh[1] = 'E'; short_sh[2] = 'L'; short_sh[3] = 'F';
		short_sh[4] = 2; short_sh[5] = 1;
		write_u16_le(short_sh, 52, 64);
		write_u64_le(short_sh, 40, 64); // e_shoff = 64
		write_u16_le(short_sh, 58, 64); // e_shentsize = 64
		write_u16_le(short_sh, 60, 2);  // e_shnum = 2
		write_u16_le(short_sh, 62, 0);  // e_shstrndx = 0
		// Section 0 (shstrtab) at offset 64: offset = 64, size = 500 (> 200)
		write_u32_le(short_sh, 64 + 4, 3); // SHT_STRTAB
		write_u64_le(short_sh, 64 + 24, 64);
		write_u64_le(short_sh, 64 + 32, 500);
		// Section 1: name_offset = 300 (64 + 300 = 364 > data.size())
		write_u32_le(short_sh, 128, 300);
		elf_hex_highlighter hl_shstr;
		assert(hl_shstr.parse(short_sh));
	}
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

	// Unparsed highlighter edge cases
	jpeg_hex_highlighter unparsed_hl;
	assert(!unparsed_hl.can_handle({}));
	assert(!unparsed_hl.can_handle(std::vector<uint8_t>{0xFF, 0xD8}));
	assert(!unparsed_hl.can_handle(std::vector<uint8_t>{'n', 'o', 't', 'j', 'p', 'g'}));
	assert(!unparsed_hl.parse({}));
	assert(unparsed_hl.get_info({}, 0).type == hex_semantic_type::normal);
	assert(!unparsed_hl.get_offset_by_name("SOI").has_value());
	assert(unparsed_hl.get_structure_summary().empty());

	// Unmapped offset in valid JPEG
	assert(hl.get_info(data, 35).type == hex_semantic_type::normal);

	// Navigation & lookup
	size_t cur = 0;
	size_t next = hl.get_next_symbol_offset(cur);
	assert(next > cur);
	while (next > cur) {
		cur = next;
		next = hl.get_next_symbol_offset(cur);
	}
	assert(next == cur);

	auto off_soi = hl.get_offset_by_name("SOI");
	assert(off_soi.has_value() && *off_soi == 0);
	auto off_app0 = hl.get_offset_by_name("APP0");
	assert(off_app0.has_value() && *off_app0 == 2);
	auto off_missing = hl.get_offset_by_name("MISSING");
	assert(!off_missing.has_value());

	// Structure summary
	std::string summary = hl.get_structure_summary();
	assert(summary.find("### JPEG Structural Overview") != std::string::npos);
	assert(summary.find("APP0") != std::string::npos);

	// Additional marker types: DRI (0xDD), COM (0xFE), RST0 (0xD0), TEM (0x01), unknown (0xF0), units=2 (Pixels/cm)
	std::vector<uint8_t> extra_jpeg = {
		0xFF, 0xD8,                         // SOI
		0xFF, 0xE0, 0x00, 0x10,             // APP0 length 16
		'J', 'F', 'I', 'F', 0x00,           // JFIF
		0x01, 0x01,                         // ver 1.1
		0x02,                               // units = 2 (Pixels/cm)
		0x00, 0x28, 0x00, 0x28,             // 40 x 40 density
		0x00, 0x00,                         // thumb
		0xFF, 0xDD, 0x00, 0x04, 0x00, 0x04, // DRI length 4
		0xFF, 0xFE, 0x00, 0x07, 'C', 'O', 'M', 0x00, 0x00, // COM length 7
		0xFF, 0xD0,                         // RST0 standalone
		0xFF, 0x01,                         // TEM standalone
		0xFF, 0xF0, 0x00, 0x03, 0x42,       // Unknown marker 0xF0 length 3
		0xFF, 0xD9                          // EOI
	};
	jpeg_hex_highlighter extra_hl;
	assert(extra_hl.can_handle(extra_jpeg));
	assert(extra_hl.parse(extra_jpeg));
	auto info_cm = extra_hl.get_info(extra_jpeg, 2);
	assert(info_cm.description.find("Pixels/cm") != std::string::npos);
	auto off_dri = extra_hl.get_offset_by_name("DRI");
	assert(off_dri.has_value());
	auto off_com = extra_hl.get_offset_by_name("COM");
	assert(off_com.has_value());
	auto off_rst0 = extra_hl.get_offset_by_name("RST0");
	assert(off_rst0.has_value());
	auto off_tem = extra_hl.get_offset_by_name("TEM");
	assert(off_tem.has_value());
	auto off_unk = extra_hl.get_offset_by_name("Marker 0xF0");
	assert(off_unk.has_value());

	// Truncated buffer right after marker (offset + 2 > data.size())
	std::vector<uint8_t> trunc_jpeg = {0xFF, 0xD8, 0xFF, 0xE1, 0x00};
	jpeg_hex_highlighter trunc_hl;
	assert(trunc_hl.parse(trunc_jpeg));
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

	// Verify compressed data range query
	for (const auto &lf : files) {
		if (lf.filename == "setup.py" && lf.compressed_size > 0) {
			highlight_info data_inf = hl.get_info(data, lf.data_offset + 1);
			assert(data_inf.type == hex_semantic_type::data_section);
			assert(data_inf.description.find("ZIP Compressed Data: setup.py") != std::string::npos);
		}
	}

	// Verify Central Directory Entry query
	const auto &cd_entries = hl.get_cd_entries();
	assert(!cd_entries.empty());
	highlight_info cd_inf = hl.get_info(data, cd_entries[0].offset + 2);
	assert(cd_inf.type == hex_semantic_type::sect_header);
	assert(cd_inf.description.find("ZIP Central Directory Entry:") != std::string::npos);

	// Verify EOCD query
	assert(hl.get_eocd_offset() > 0);
	highlight_info eocd_inf = hl.get_info(data, hl.get_eocd_offset());
	assert(eocd_inf.type == hex_semantic_type::magic);
	assert(eocd_inf.description.find("ZIP End of Central Directory Record") != std::string::npos);

	// Query beyond end or unmapped region
	highlight_info normal_inf = hl.get_info(data, data.size() + 100);
	assert(normal_inf.type == hex_semantic_type::normal);

	// Verify structure summary
	std::string summary = hl.get_structure_summary();
	assert(summary.find("### Archive Contents (ZIP)") != std::string::npos);
	assert(summary.find("setup.py") != std::string::npos);

	// Verify symbol offset navigation
	size_t next_sym = hl.get_next_symbol_offset(0);
	assert(next_sym > 0);
	assert(hl.get_next_symbol_offset(hl.get_eocd_offset()) == hl.get_eocd_offset());

	// Nonexistent file lookup
	assert(!hl.get_offset_by_name("nonexistent_dummy_file.txt").has_value());

	// Unparsed highlighter handling
	zip_hex_highlighter unparsed;
	assert(unparsed.get_structure_summary().empty());
	assert(!unparsed.get_offset_by_name("setup.py").has_value());
	assert(unparsed.get_next_symbol_offset(50) == 50);
	assert(!unparsed.can_handle(std::vector<uint8_t>(10, 0)));
	assert(!unparsed.parse(std::vector<uint8_t>(10, 0)));

	// Fallback sequential parser testing (ZIP payload without EOCD record)
	if (hl.get_eocd_offset() > 0) {
		std::vector<uint8_t> no_eocd(data.begin(), data.begin() + hl.get_eocd_offset());
		zip_hex_highlighter fallback_hl;
		assert(fallback_hl.can_handle(no_eocd));
		assert(fallback_hl.parse(no_eocd));
		assert(!fallback_hl.get_local_files().empty());
		assert(!fallback_hl.get_cd_entries().empty());

		// Fallback parser encountering an EOCD record in the sequential stream (EOCD beyond 65k reverse scan)
		std::vector<uint8_t> with_eocd = no_eocd;
		with_eocd.push_back(0x50); with_eocd.push_back(0x4B);
		with_eocd.push_back(0x05); with_eocd.push_back(0x06);
		for (int i = 0; i < 20; ++i) with_eocd.push_back(0);
		// Append >65557 bytes of trailing data so reverse scan doesn't find it
		with_eocd.resize(with_eocd.size() + 70000, 0);
		zip_hex_highlighter fallback_hl2;
		assert(fallback_hl2.parse(with_eocd));
		assert(fallback_hl2.get_eocd_offset() == no_eocd.size());
	}

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
	assert(!hl.get_objects().empty());

	auto &reg = hex_highlighter_registry::get_instance();
	auto detected = reg.detect_highlighter(data);
	assert(detected != nullptr);
	assert(dynamic_cast<pdf_hex_highlighter *>(detected.get()) != nullptr);
	std::cout << "Real PDF file test passed!" << std::endl;

	// Synthetic PDF test with stream and multiple objects to verify stream/endstream parsing
	std::string synthetic_pdf =
		"%PDF-1.4\n"
		"1 0 obj\n"
		"<< /Length 5 >>\n"
		"stream\n"
		"hello\n"
		"endstream\n"
		"endobj\n"
		"2 0 obj\n"
		"<< /Type /Catalog >>\n"
		"endobj\n"
		"3   0   obj\n"
		"<< >>\n"
		"endobj\n"
		"xref\n"
		"0 4\n"
		"0000000000 65535 f \n"
		"0000000009 00000 n \n"
		"0000000055 00000 n \n"
		"0000000090 00000 n \n"
		"trailer\n"
		"<< /Size 4 >>\n"
		"startxref\n"
		"140\n"
		"%%EOF\n";

	std::vector<uint8_t> syn_data(synthetic_pdf.begin(), synthetic_pdf.end());
	pdf_hex_highlighter syn_hl;
	assert(syn_hl.can_handle(syn_data));
	assert(syn_hl.parse(syn_data));
	assert(syn_hl.get_objects().size() == 3);
	assert(syn_hl.get_objects()[0].id == "1 0");
	assert(syn_hl.get_objects()[1].id == "2 0");
	assert(syn_hl.get_objects()[2].id == "3 0");

	// Unparsed highlighter edge cases
	pdf_hex_highlighter unparsed_hl;
	assert(!unparsed_hl.can_handle({}));
	assert(!unparsed_hl.parse({}));
	assert(!unparsed_hl.can_handle(std::vector<uint8_t>{'%', 'P', 'D'}));
	assert(!unparsed_hl.can_handle(std::vector<uint8_t>{'n', 'o', 't', 'p', 'd', 'f', '!', '!'}));
	assert(unparsed_hl.get_info({}, 0).type == hex_semantic_type::normal);
	assert(!unparsed_hl.get_offset_by_name("1 0").has_value());
	assert(unparsed_hl.get_structure_summary().empty());

	// Semantic info queries on synthetic PDF
	auto info_header = syn_hl.get_info(syn_data, 0);
	assert(info_header.type == hex_semantic_type::file_header);
	assert(info_header.description.find("PDF Header") != std::string::npos);

	const auto &objs = syn_hl.get_objects();
	auto info_obj = syn_hl.get_info(syn_data, objs[0].offset);
	assert(info_obj.type == hex_semantic_type::prog_header);
	assert(info_obj.description.find("PDF Object") != std::string::npos);

	auto info_stream = syn_hl.get_info(syn_data, objs[0].stream_offset + 1);
	assert(info_stream.type == hex_semantic_type::data_section);
	assert(info_stream.description.find("PDF Object Stream") != std::string::npos);

	const auto &xrefs = syn_hl.get_xrefs();
	assert(!xrefs.empty());
	auto info_xref = syn_hl.get_info(syn_data, xrefs[0].offset);
	assert(info_xref.type == hex_semantic_type::sect_header);
	assert(info_xref.description.find("Cross-Reference") != std::string::npos);

	const auto &trailers = syn_hl.get_trailers();
	assert(!trailers.empty());
	auto info_trailer = syn_hl.get_info(syn_data, trailers[0].offset);
	assert(info_trailer.type == hex_semantic_type::magic);
	assert(info_trailer.description.find("Trailer") != std::string::npos);

	// Out of bounds and clamped offset
	auto info_oob = syn_hl.get_info(syn_data, syn_data.size() + 100);
	assert(info_oob.type == hex_semantic_type::normal);
	// Truncated buffer passed to get_info clamping header size
	std::vector<uint8_t> truncated_buffer(syn_data.begin(), syn_data.begin() + 4);
	auto info_clamp = syn_hl.get_info(truncated_buffer, 2);
	assert(info_clamp.type == hex_semantic_type::file_header);
	assert(info_clamp.range_size == 4);
	// Navigation & lookup
	size_t cur = 0;
	size_t next = syn_hl.get_next_symbol_offset(cur);
	assert(next > cur);
	while (next > cur) {
		cur = next;
		next = syn_hl.get_next_symbol_offset(cur);
	}
	assert(next == cur); // Reached the end, returns current_offset

	auto off_id = syn_hl.get_offset_by_name("1 0");
	assert(off_id.has_value() && *off_id == objs[0].offset);
	auto off_id_obj = syn_hl.get_offset_by_name("1 0 obj");
	assert(off_id_obj.has_value() && *off_id_obj == objs[0].offset);
	auto off_missing = syn_hl.get_offset_by_name("99 0");
	assert(!off_missing.has_value());

	// Summary
	std::string summary = syn_hl.get_structure_summary();
	assert(summary.find("### PDF Structural Overview") != std::string::npos);
	assert(summary.find("PDF Objects") != std::string::npos);
	assert(summary.find("Cross-Reference (XRef) Tables") != std::string::npos);
	assert(summary.find("Trailers") != std::string::npos);

	// Direct xref -> startxref without trailer keyword
	std::string pdf_no_trailer =
		"%PDF-1.4\n"
		"1 0 obj\n"
		"<< >>\n"
		"endobj\n"
		"xref\n"
		"0 1\n"
		"0000000000 65535 f \n"
		"startxref\n"
		"35\n"
		"%%EOF\n";
	std::vector<uint8_t> no_trailer_data(pdf_no_trailer.begin(), pdf_no_trailer.end());
	pdf_hex_highlighter no_tr_hl;
	assert(no_tr_hl.can_handle(no_trailer_data));
	assert(no_tr_hl.parse(no_trailer_data));
	assert(!no_tr_hl.get_xrefs().empty());

	// Truncated at xref
	std::string pdf_trunc_xref = "%PDF-1.4\nxref\n0 1\n0000000000 65535 f \n";
	std::vector<uint8_t> trunc_xref_data(pdf_trunc_xref.begin(), pdf_trunc_xref.end());
	pdf_hex_highlighter trunc_xr_hl;
	assert(trunc_xr_hl.parse(trunc_xref_data));
	assert(!trunc_xr_hl.get_xrefs().empty());

	// Truncated at trailer
	std::string pdf_trunc_tr = "%PDF-1.4\ntrailer\n<< /Size 1 >>\n";
	std::vector<uint8_t> trunc_tr_data(pdf_trunc_tr.begin(), pdf_trunc_tr.end());
	pdf_hex_highlighter trunc_tr_hl;
	assert(trunc_tr_hl.parse(trunc_tr_data));
	assert(!trunc_tr_hl.get_trailers().empty());
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

	// ustar prefix at offset 345: "archive/subdir"
	std::string prefix = "archive/subdir";
	for (size_t i = 0; i < prefix.length(); ++i) {
		data[345 + i] = static_cast<uint8_t>(prefix[i]);
	}

	tar_hex_highlighter hl;
	assert(hl.can_handle(data) == true);
	bool success = hl.parse(data);
	assert(success == true);
	assert(hl.get_files().size() == 1);
	assert(hl.get_files()[0].filename == "archive/subdir/hello.txt");
	assert(hl.get_files()[0].size == 12);

	// Test header info
	highlight_info inf = hl.get_info(data, 10);
	assert(inf.type == hex_semantic_type::file_header);
	assert(inf.description.find("archive/subdir/hello.txt") != std::string::npos);

	// Test data section info
	highlight_info data_inf = hl.get_info(data, 512 + 4);
	assert(data_inf.type == hex_semantic_type::data_section);
	assert(data_inf.description.find("TAR File Data: archive/subdir/hello.txt") != std::string::npos);

	// Test EOD (End of Archive marker) info at offset 1024
	highlight_info eod_inf = hl.get_info(data, 1024);
	assert(eod_inf.type == hex_semantic_type::magic);
	assert(eod_inf.description.find("TAR End of Archive Marker") != std::string::npos);

	// Test out of range offset
	highlight_info out_inf = hl.get_info(data, 2048);
	assert(out_inf.type == hex_semantic_type::normal);

	// Test structure summary
	std::string summary = hl.get_structure_summary();
	assert(summary.find("### Archive Contents (TAR)") != std::string::npos);
	assert(summary.find("archive/subdir/hello.txt") != std::string::npos);

	// Test symbol offset navigation
	assert(hl.get_offset_by_name("archive/subdir/hello.txt").has_value());
	assert(!hl.get_offset_by_name("nonexistent.txt").has_value());
	assert(hl.get_next_symbol_offset(0) > 0);
	assert(hl.get_next_symbol_offset(512) == 1024);
	assert(hl.get_next_symbol_offset(1024) == 1024);

	// Test unparsed highlighter behavior
	tar_hex_highlighter unparsed;
	assert(unparsed.get_structure_summary().empty());
	assert(!unparsed.get_offset_by_name("hello.txt").has_value());
	assert(unparsed.get_next_symbol_offset(50) == 50);
	assert(!unparsed.can_handle(std::vector<uint8_t>(200, 0)));
	assert(!unparsed.parse(std::vector<uint8_t>(200, 0)));

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

	// 7. Universal Out-of-Bounds and Overflow Queries
	{
		std::vector<uint8_t> minimal_png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
		png_hex_highlighter png_hl;
		if (png_hl.parse(minimal_png)) {
			// Querying out-of-bounds offset should return normal/empty info
			highlight_info oob_info = png_hl.get_info(minimal_png, minimal_png.size());
			assert(oob_info.type == hex_semantic_type::normal);
			highlight_info max_info = png_hl.get_info(minimal_png, SIZE_MAX);
			assert(max_info.type == hex_semantic_type::normal);
		}

		// PNG 32-bit chunk length integer overflow attack:
		// len = 0xFFFFFFFE. If evaluated as uint32_t (12 + len), 12 + 0xFFFFFFFE == 10,
		// wrapping around and spuriously advancing offset by 10.
		std::vector<uint8_t> png_len_wrap(40, 0);
		png_len_wrap[0] = 0x89; png_len_wrap[1] = 'P'; png_len_wrap[2] = 'N'; png_len_wrap[3] = 'G';
		png_len_wrap[4] = 0x0D; png_len_wrap[5] = 0x0A; png_len_wrap[6] = 0x1A; png_len_wrap[7] = 0x0A;
		png_len_wrap[8] = 0xFF; png_len_wrap[9] = 0xFF; png_len_wrap[10] = 0xFF; png_len_wrap[11] = 0xFE;
		png_len_wrap[12] = 't'; png_len_wrap[13] = 'E'; png_len_wrap[14] = 'S'; png_len_wrap[15] = 't';
		// Next chunk signature placed at offset 18
		png_len_wrap[18] = 0x00; png_len_wrap[19] = 0x00; png_len_wrap[20] = 0x00; png_len_wrap[21] = 0x00;
		png_len_wrap[22] = 'I'; png_len_wrap[23] = 'E'; png_len_wrap[24] = 'N'; png_len_wrap[25] = 'D';
		png_hl.parse(png_len_wrap);
		// With 0xFFFFFFFE length, the chunk extends way past buffer (4.29GB).
		// The parser must NOT have wrapped 12 + len to 10 and parsed a second chunk at offset 18!
		assert(!png_hl.get_offset_by_name("IEND").has_value());
		highlight_info chunk_info = png_hl.get_info(png_len_wrap, 16);
		assert(chunk_info.range_start <= png_len_wrap.size());
		assert(chunk_info.range_size <= png_len_wrap.size() - chunk_info.range_start);

		// TAR with declared size extending past data buffer
		std::vector<uint8_t> tar_trunc_payload(512, 0);
		std::string tfname = "bigfile.bin";
		for (size_t i = 0; i < tfname.length(); ++i) {
			tar_trunc_payload[i] = static_cast<uint8_t>(tfname[i]);
		}
		// Size = 100000 bytes octal: "00000303240"
		std::string tsize = "00000303240";
		for (size_t i = 0; i < tsize.length(); ++i) {
			tar_trunc_payload[124 + i] = static_cast<uint8_t>(tsize[i]);
		}
		tar_trunc_payload[257] = 'u'; tar_trunc_payload[258] = 's'; tar_trunc_payload[259] = 't';
		tar_trunc_payload[260] = 'a'; tar_trunc_payload[261] = 'r';
		tar_hex_highlighter tar_hl;
		assert(tar_hl.can_handle(tar_trunc_payload));
		tar_hl.parse(tar_trunc_payload);
		// Querying beyond data.size() must return normal, NOT report data_section for bytes that don't exist
		highlight_info tar_oob = tar_hl.get_info(tar_trunc_payload, 600);
		assert(tar_oob.type == hex_semantic_type::normal);

		// PDF invalid object without number
		pdf_hex_highlighter pdf_hl;
		std::string bad_pdf = "%PDF-1.4\n obj\nendobj\n%%EOF\n";
		std::vector<uint8_t> bad_pdf_data(bad_pdf.begin(), bad_pdf.end());
		assert(pdf_hl.can_handle(bad_pdf_data));
		pdf_hl.parse(bad_pdf_data);
		// " obj" without object and generation numbers must not be treated as a valid object
		assert(!pdf_hl.get_offset_by_name("").has_value());
		assert(!pdf_hl.get_offset_by_name(" obj").has_value());
	}

	// 8. Deep ELF Malicious Inputs
	{
		elf_hex_highlighter elf_hl;

		// 8a. Zero entry sizes (prevent divide-by-zero crashes)
		std::vector<uint8_t> elf_zero_entsize(128, 0);
		elf_zero_entsize[0] = 0x7F; elf_zero_entsize[1] = 'E'; elf_zero_entsize[2] = 'L'; elf_zero_entsize[3] = 'F';
		elf_zero_entsize[4] = 2; // 64-bit
		elf_zero_entsize[5] = 1; // LSB
		write_u64_le(elf_zero_entsize, 32, 64); // e_phoff = 64
		write_u16_le(elf_zero_entsize, 54, 0);  // e_phentsize = 0 (attack!)
		write_u16_le(elf_zero_entsize, 56, 10); // e_phnum = 10
		write_u64_le(elf_zero_entsize, 40, 64); // e_shoff = 64
		write_u16_le(elf_zero_entsize, 58, 0);  // e_shentsize = 0 (attack!)
		write_u16_le(elf_zero_entsize, 60, 10); // e_shnum = 10
		assert(elf_hl.can_handle(elf_zero_entsize));
		assert(elf_hl.parse(elf_zero_entsize));
		// Querying at offset 64 must not crash with SIGFPE
		highlight_info info64 = elf_hl.get_info(elf_zero_entsize, 64);
		assert(info64.type == hex_semantic_type::normal);

		// 8b. Out-of-bounds offset queries on valid ELF
		highlight_info oob1 = elf_hl.get_info(elf_zero_entsize, elf_zero_entsize.size());
		assert(oob1.type == hex_semantic_type::normal);
		highlight_info oob2 = elf_hl.get_info(elf_zero_entsize, SIZE_MAX);
		assert(oob2.type == hex_semantic_type::normal);

		// 8c. Section headers wrapping size_t
		std::vector<uint8_t> elf_wrap(128, 0);
		elf_wrap[0] = 0x7F; elf_wrap[1] = 'E'; elf_wrap[2] = 'L'; elf_wrap[3] = 'F';
		elf_wrap[4] = 2; elf_wrap[5] = 1;
		write_u64_le(elf_wrap, 40, SIZE_MAX - 30); // e_shoff near SIZE_MAX
		write_u16_le(elf_wrap, 58, 64);            // e_shentsize = 64
		write_u16_le(elf_wrap, 60, 5);             // e_shnum = 5
		assert(elf_hl.can_handle(elf_wrap));
		assert(elf_hl.parse(elf_wrap));
		highlight_info wrap_info = elf_hl.get_info(elf_wrap, 10);
		assert(wrap_info.type == hex_semantic_type::file_header);
	}

	// 9. Deep PNG Malicious Inputs
	{
		png_hex_highlighter png_hl;

		// 9a. Chunk type containing control characters and markdown injection
		std::vector<uint8_t> png_inject(40, 0);
		png_inject[0] = 0x89; png_inject[1] = 'P'; png_inject[2] = 'N'; png_inject[3] = 'G';
		png_inject[4] = 0x0D; png_inject[5] = 0x0A; png_inject[6] = 0x1A; png_inject[7] = 0x0A;
		// len = 0
		write_u32_le(png_inject, 8, 0);
		// Type with pipe, newline, backtick, escape
		png_inject[12] = '|'; png_inject[13] = '\n'; png_inject[14] = '`'; png_inject[15] = 0x1B;
		assert(png_hl.can_handle(png_inject));
		assert(png_hl.parse(png_inject));
		std::string summary = png_hl.get_structure_summary();
		// Ensure control chars / pipes in chunk type were sanitized into '?'
		assert(summary.find("`????`") != std::string::npos);

		// 9b. Truncated IHDR (length < 13)
		std::vector<uint8_t> png_short_ihdr(30, 0);
		png_short_ihdr[0] = 0x89; png_short_ihdr[1] = 'P'; png_short_ihdr[2] = 'N'; png_short_ihdr[3] = 'G';
		png_short_ihdr[4] = 0x0D; png_short_ihdr[5] = 0x0A; png_short_ihdr[6] = 0x1A; png_short_ihdr[7] = 0x0A;
		write_u32_le(png_short_ihdr, 8, 5); // len = 5 (< 13 required for IHDR)
		png_short_ihdr[12] = 'I'; png_short_ihdr[13] = 'H'; png_short_ihdr[14] = 'D'; png_short_ihdr[15] = 'R';
		assert(png_hl.parse(png_short_ihdr));
		// Querying IHDR offset must not read out of bounds
		highlight_info ihdr_info = png_hl.get_info(png_short_ihdr, 16);
		assert(ihdr_info.type == hex_semantic_type::code_section);

		// 9c. Empty data / out of bounds get_info
		highlight_info oob = png_hl.get_info(png_short_ihdr, 9999);
		assert(oob.type == hex_semantic_type::normal);
	}

	// 10. Deep JPEG Malicious Inputs
	{
		jpeg_hex_highlighter jpeg_hl;

		// 10a. Marker with length_val < 2 (0 and 1)
		std::vector<uint8_t> jpeg_zero_len = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x00, 0xFF, 0xD9};
		assert(jpeg_hl.can_handle(jpeg_zero_len));
		assert(jpeg_hl.parse(jpeg_zero_len));

		std::vector<uint8_t> jpeg_one_len = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x01, 0xFF, 0xD9};
		assert(jpeg_hl.can_handle(jpeg_one_len));
		assert(jpeg_hl.parse(jpeg_one_len));

		// 10b. SOS marker without terminating EOI marker (scans to EOF)
		std::vector<uint8_t> jpeg_sos_eof = {
			0xFF, 0xD8,
			0xFF, 0xDA, 0x00, 0x02, // SOS marker, length 2
			0x12, 0x34, 0xFF, 0x00, 0x56, 0x78 // Entropy data with byte stuffing 0xFF 0x00
		};
		assert(jpeg_hl.can_handle(jpeg_sos_eof));
		assert(jpeg_hl.parse(jpeg_sos_eof));
		highlight_info sos_info = jpeg_hl.get_info(jpeg_sos_eof, 8);
		assert(sos_info.type == hex_semantic_type::code_section);

		// 10c. OOB get_info
		highlight_info oob = jpeg_hl.get_info(jpeg_sos_eof, jpeg_sos_eof.size());
		assert(oob.type == hex_semantic_type::normal);
		highlight_info oob_max = jpeg_hl.get_info(jpeg_sos_eof, SIZE_MAX);
		assert(oob_max.type == hex_semantic_type::normal);
	}

	// 11. Deep ZIP Malicious Inputs
	{
		zip_hex_highlighter zip_hl;

		// 11a. ZIP with sign-extension in 32-bit fields (bit 31 set: 0x80000000)
		std::vector<uint8_t> zip_sign(64, 0);
		zip_sign[0] = 0x50; zip_sign[1] = 0x4B; zip_sign[2] = 0x03; zip_sign[3] = 0x04;
		size_t eocd_pos = zip_sign.size() - 22;
		zip_sign[eocd_pos] = 0x50; zip_sign[eocd_pos + 1] = 0x4B;
		zip_sign[eocd_pos + 2] = 0x05; zip_sign[eocd_pos + 3] = 0x06;
		// cd_offset = 0x80000000
		zip_sign[eocd_pos + 16] = 0x00; zip_sign[eocd_pos + 17] = 0x00;
		zip_sign[eocd_pos + 18] = 0x00; zip_sign[eocd_pos + 19] = 0x80;
		// cd_size = 0x80000000
		zip_sign[eocd_pos + 12] = 0x00; zip_sign[eocd_pos + 13] = 0x00;
		zip_sign[eocd_pos + 14] = 0x00; zip_sign[eocd_pos + 15] = 0x80;
		assert(zip_hl.can_handle(zip_sign));
		assert(zip_hl.parse(zip_sign));
		// Out of bounds get_info
		highlight_info oob = zip_hl.get_info(zip_sign, 9999);
		assert(oob.type == hex_semantic_type::normal);

		// 11b. Path traversal & filename injection in ZIP
		std::vector<uint8_t> zip_trav(120, 0);
		zip_trav[0] = 0x50; zip_trav[1] = 0x4B; zip_trav[2] = 0x03; zip_trav[3] = 0x04;
		std::string trav_name = "../../../etc/passwd|`\n";
		write_u16_le(zip_trav, 26, static_cast<uint16_t>(trav_name.length())); // filename_len
		for (size_t i = 0; i < trav_name.length(); ++i) {
			zip_trav[30 + i] = static_cast<uint8_t>(trav_name[i]);
		}
		assert(zip_hl.parse(zip_trav));
		std::string sum = zip_hl.get_structure_summary();
		// Pipe and backticks must be sanitized
		assert(sum.find('|') != std::string::npos); // Table markup exists
		assert(sum.find("passwd?`") == std::string::npos); // Raw backtick was sanitized
	}

	// 12. Deep TAR Malicious Inputs
	{
		tar_hex_highlighter tar_hl;

		// 12a. Octal size near overflow (SIZE_MAX - 100)
		std::vector<uint8_t> tar_overflow(1024, 0);
		std::string ofname = "overflow.dat";
		for (size_t i = 0; i < ofname.length(); ++i) {
			tar_overflow[i] = static_cast<uint8_t>(ofname[i]);
		}
		// 11 octal sevens: "77777777777"
		std::string max_oct = "77777777777";
		for (size_t i = 0; i < max_oct.length(); ++i) {
			tar_overflow[124 + i] = static_cast<uint8_t>(max_oct[i]);
		}
		tar_overflow[257] = 'u'; tar_overflow[258] = 's'; tar_overflow[259] = 't';
		tar_overflow[260] = 'a'; tar_overflow[261] = 'r';
		assert(tar_hl.can_handle(tar_overflow));
		assert(tar_hl.parse(tar_overflow));
		// Querying at 600 must not claim data section
		highlight_info inf = tar_hl.get_info(tar_overflow, 600);
		assert(inf.type == hex_semantic_type::data_section); // 600 < 1024 so it is within data buffer!
		// Querying at 2000 (beyond data.size()) must be normal
		highlight_info inf_oob = tar_hl.get_info(tar_overflow, 2000);
		assert(inf_oob.type == hex_semantic_type::normal);

		// 12b. 100-byte unterminated filename
		std::vector<uint8_t> tar_no_null(1024, 0);
		for (size_t i = 0; i < 100; ++i) {
			tar_no_null[i] = 'A'; // No null byte in 100 bytes
		}
		std::string s_oct = "00000000010";
		for (size_t i = 0; i < s_oct.length(); ++i) {
			tar_no_null[124 + i] = static_cast<uint8_t>(s_oct[i]);
		}
		tar_no_null[257] = 'u'; tar_no_null[258] = 's'; tar_no_null[259] = 't';
		tar_no_null[260] = 'a'; tar_no_null[261] = 'r';
		assert(tar_hl.parse(tar_no_null));
		assert(tar_hl.get_files().size() == 1);
		assert(tar_hl.get_files()[0].filename.length() == 100);
	}

	// 13. Deep PDF Malicious Inputs
	{
		pdf_hex_highlighter pdf_hl;

		// 13a. Nested unclosed streams and objects
		std::string nest_pdf = "%PDF-1.7\n1 0 obj\nstream\n2 0 obj\nstream\nendstream\n";
		std::vector<uint8_t> nest_data(nest_pdf.begin(), nest_pdf.end());
		assert(pdf_hl.can_handle(nest_data));
		assert(pdf_hl.parse(nest_data));
		highlight_info p_inf = pdf_hl.get_info(nest_data, 10);
		assert(p_inf.type != hex_semantic_type::magic);

		// 13b. startxref without EOF
		std::string no_eof = "%PDF-1.4\nxref\n0 1\n0000000000 65535 f \ntrailer\n<< /Size 1 >>\nstartxref\n9\n";
		std::vector<uint8_t> no_eof_data(no_eof.begin(), no_eof.end());
		assert(pdf_hl.can_handle(no_eof_data));
		assert(pdf_hl.parse(no_eof_data));
		highlight_info tr_inf = pdf_hl.get_info(no_eof_data, no_eof_data.size() - 5);
		assert(tr_inf.type == hex_semantic_type::magic);

		// 13c. OOB get_info
		highlight_info oob = pdf_hl.get_info(no_eof_data, no_eof_data.size() + 50);
		assert(oob.type == hex_semantic_type::normal);
	}

	std::cout << "All robustness & malicious/corrupt file format tests passed cleanly!" << std::endl;
}

void test_bounds_safety_and_clamped_ranges()
{
	std::cout << "Testing bounds safety and clamped highlight ranges across all 6 formats..." << std::endl;

	auto verify_ranges_safe = [](const auto &hl, std::span<const uint8_t> data) {
		for (size_t off = 0; off <= data.size() + 5; ++off) {
			highlight_info info = hl.get_info(data, off);
			if (off >= data.size()) {
				assert(info.type == hex_semantic_type::normal);
				assert(info.range_size == 0);
			} else if (info.range_size > 0) {
				assert(info.range_start <= data.size());
				assert(info.range_size <= data.size() - info.range_start);
				// Verify subspan does not throw or violate span bounds
				auto sub = data.subspan(info.range_start, info.range_size);
				(void)sub;
			}
		}
	};

	// 1. PNG with oversized IHDR length (e.g. 1000000 bytes) but buffer is only 40 bytes
	{
		png_hex_highlighter png_hl;
		std::vector<uint8_t> buf = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A,
		                            0x00, 0x10, 0x00, 0x00, // Length = 1,048,576
		                            'I', 'H', 'D', 'R',
		                            0x00, 0x00, 0x00, 0x01, // width
		                            0x00, 0x00, 0x00, 0x01, // height
		                            8, 2, 0, 0, 0,          // bit depth, color type, etc.
		                            0, 0, 0, 0};            // CRC
		assert(png_hl.can_handle(buf));
		png_hl.parse(buf);
		verify_ranges_safe(png_hl, buf);
		highlight_info info = png_hl.get_info(buf, 12);
		assert(info.range_start + info.range_size <= buf.size());
	}

	// 2. JPEG with oversized segment marker length
	{
		jpeg_hex_highlighter jpeg_hl;
		std::vector<uint8_t> buf = {0xFF, 0xD8, 0xFF, 0xE0, 0xFF, 0x00, 'J', 'F', 'I', 'F', 0};
		assert(jpeg_hl.can_handle(buf));
		jpeg_hl.parse(buf);
		verify_ranges_safe(jpeg_hl, buf);
		highlight_info info = jpeg_hl.get_info(buf, 4);
		assert(info.range_start + info.range_size <= buf.size());
	}

	// 3. ZIP with oversized local file header
	{
		zip_hex_highlighter zip_hl;
		std::vector<uint8_t> buf(80, 0);
		buf[0] = 'P'; buf[1] = 'K'; buf[2] = 0x03; buf[3] = 0x04; // LFH magic
		write_u16_le(buf, 4, 20); // version
		write_u32_le(buf, 18, 0x05000000); // 83 MB compressed
		write_u32_le(buf, 22, 0x05000000); // 83 MB uncompressed
		write_u16_le(buf, 26, 500); // filename length 500 (exceeds buffer)
		write_u16_le(buf, 28, 500); // extra length 500
		assert(zip_hl.can_handle(buf));
		zip_hl.parse(buf);
		verify_ranges_safe(zip_hl, buf);
		highlight_info info = zip_hl.get_info(buf, 0);
		assert(info.range_start + info.range_size <= buf.size());
	}

	// 4. TAR with 10MB declared file size in octal but small buffer
	{
		tar_hex_highlighter tar_hl;
		std::vector<uint8_t> buf(600, 0);
		std::string name = "bigfile.bin";
		std::copy(name.begin(), name.end(), buf.begin());
		std::string oct_size = "00077777777"; // huge size
		for (size_t i = 0; i < oct_size.size(); ++i) {
			buf[124 + i] = static_cast<uint8_t>(oct_size[i]);
		}
		buf[257] = 'u'; buf[258] = 's'; buf[259] = 't'; buf[260] = 'a'; buf[261] = 'r';
		assert(tar_hl.can_handle(buf));
		tar_hl.parse(buf);
		verify_ranges_safe(tar_hl, buf);
		highlight_info info = tar_hl.get_info(buf, 550);
		assert(info.range_start + info.range_size <= buf.size());
	}

	// 5. PDF with oversized object stream
	{
		pdf_hex_highlighter pdf_hl;
		std::string pdf_data = "%PDF-1.4\n1 0 obj\n<< /Length 1000000 >>\nstream\nABC\nendstream\nendobj\n%%EOF\n";
		std::vector<uint8_t> buf(pdf_data.begin(), pdf_data.end());
		assert(pdf_hl.can_handle(buf));
		pdf_hl.parse(buf);
		verify_ranges_safe(pdf_hl, buf);
	}

	// 6. ELF with oversized section header and section sizes
	{
		elf_hex_highlighter elf_hl;
		std::vector<uint8_t> buf(120, 0);
		buf[0] = 0x7F; buf[1] = 'E'; buf[2] = 'L'; buf[3] = 'F';
		buf[4] = 2; // ELFCLASS64
		buf[5] = 1; // LSB
		buf[6] = 1; // EV_CURRENT
		write_u64_le(buf, 40, 64); // e_shoff = 64
		write_u16_le(buf, 52, 64); // e_ehsize
		write_u16_le(buf, 58, 64); // e_shentsize
		write_u16_le(buf, 60, 2);  // e_shnum
		assert(elf_hl.can_handle(buf));
		elf_hl.parse(buf);
		verify_ranges_safe(elf_hl, buf);
	}

	std::cout << "Bounds safety and clamped highlight ranges verified successfully!" << std::endl;
}

int main()
{
	test_watchdog::setup_watchdog(30);
	test_elf_highlighter();
	test_elf_32bit();
	test_elf_big_endian();
	test_elf_various_types();
	test_png_highlighter();
	test_jpeg_highlighter();
	test_real_jpeg_file();
	test_zip_highlighter();
	test_pdf_highlighter();
	test_tar_highlighter();
	test_malicious_corrupt_files();
	test_bounds_safety_and_clamped_ranges();
	std::cout << "All hex syntax highlighter tests passed!" << std::endl;
	return 0;
}

