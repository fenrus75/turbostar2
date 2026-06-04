#include <cassert>
#include <iostream>
#include <memory>
#include <vector>
#include "ui/hex_highlighter.h"

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
	assert(inf.description.find("push rcx") != std::string::npos);
	assert(inf.description.find("in my_func") != std::string::npos);
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
}

int main()
{
	test_elf_highlighter();
	std::cout << "All hex syntax highlighter tests passed!" << std::endl;
	return 0;
}
