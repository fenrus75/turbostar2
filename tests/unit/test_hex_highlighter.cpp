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
	std::vector<uint8_t> data(500, 0);

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
	write_u16_le(data, 60, 3);	    // e_shnum = 3 (Null, .text, .shstrtab)
	write_u16_le(data, 62, 2);	    // e_shstrndx = 2 (.shstrtab is index 2)

	// 2. Phdr (64..119)
	size_t ph_start = 64;
	write_u32_le(data, ph_start, 1);	     // p_type = PT_LOAD
	write_u32_le(data, ph_start + 4, 5);	     // p_flags = PF_R | PF_X
	write_u64_le(data, ph_start + 8, 320);	     // p_offset = 320 (.text starts at 320)
	write_u64_le(data, ph_start + 16, 0x401000); // p_vaddr
	write_u64_le(data, ph_start + 24, 0x401000); // p_paddr
	write_u64_le(data, ph_start + 32, 32);	     // p_filesz = 32
	write_u64_le(data, ph_start + 40, 32);	     // p_memsz = 32
	write_u64_le(data, ph_start + 48, 4096);     // p_align = 4KB

	// 3. String Table Data (starts at 360)
	// Indices:
	// 0: "\0"
	// 1: ".text._ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEC2EvQ26is_default_constructible\0"
	// 93: ".shstrtab\0"
	size_t strtab_start = 360;
	std::string text_name = ".text._ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEC2EvQ26is_default_constructible";
	std::string strtab_name = ".shstrtab";
	std::copy(text_name.begin(), text_name.end(), &data[strtab_start + 1]);
	std::copy(strtab_name.begin(), strtab_name.end(), &data[strtab_start + 93]);

	// 4. Shdr [0] - Null Section (128..191) is all zeroes

	// 5. Shdr [1] - .text Section (192..255)
	size_t sh1_start = 128 + 64;
	write_u32_le(data, sh1_start, 1);	      // sh_name = 1 (".text._ZNSt7__cxx1112basic_stringIc...")
	write_u32_le(data, sh1_start + 4, 1);	      // sh_type = SHT_PROGBITS
	write_u64_le(data, sh1_start + 8, 6);	      // sh_flags = SHF_ALLOC | SHF_EXECINSTR
	write_u64_le(data, sh1_start + 16, 0x401000); // sh_addr
	write_u64_le(data, sh1_start + 24, 320);      // sh_offset = 320
	write_u64_le(data, sh1_start + 32, 32);	      // sh_size = 32

	// 6. Shdr [2] - .shstrtab Section (256..319)
	size_t sh2_start = 128 + 128;
	write_u32_le(data, sh2_start, 93);	 // sh_name = 93 (".shstrtab")
	write_u32_le(data, sh2_start + 4, 3);	 // sh_type = SHT_STRTAB
	write_u64_le(data, sh2_start + 8, 0);	 // sh_flags = 0
	write_u64_le(data, sh2_start + 16, 0);	 // sh_addr = 0
	write_u64_le(data, sh2_start + 24, 360); // sh_offset = 360
	write_u64_le(data, sh2_start + 32, 110); // sh_size = 110

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

	// Test Program Header Table range (PHT starts at 64, entries size 56)
	inf = hl.get_info(data, 64); // p_type field of entry 0
	assert(inf.type == hex_semantic_type::prog_header);
	assert(inf.description.find("p_type") != std::string::npos);
	assert(inf.description.find("PT_LOAD") != std::string::npos);

	inf = hl.get_info(data, 68); // p_flags field of entry 0
	assert(inf.type == hex_semantic_type::prog_header);
	assert(inf.description.find("p_flags") != std::string::npos);
	assert(inf.description.find("X R") != std::string::npos);

	// Test Section Header Table range (SHT starts at 128, entries size 64)
	inf = hl.get_info(data, 192); // sh_name field of SHT[1]
	assert(inf.type == hex_semantic_type::sect_header);
	assert(inf.description.find("sh_name") != std::string::npos);
	assert(inf.description.find(".text.std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >::basic_string() [requires is_default_constructible]") != std::string::npos);

	inf = hl.get_info(data, 196); // sh_type field of SHT[1]
	assert(inf.type == hex_semantic_type::sect_header);
	assert(inf.description.find("sh_type") != std::string::npos);
	assert(inf.description.find("SHT_PROGBITS") != std::string::npos);

	// Test .text section body range (mapped range: offset 320 to 351)
	inf = hl.get_info(data, 320);
	assert(inf.type == hex_semantic_type::code_section);
	assert(inf.description.find("Sec \".text.std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >::basic_string() [requires is_default_constructible]\"") != std::string::npos);

	// Test .shstrtab section body range (mapped range: offset 360 to 379)
	inf = hl.get_info(data, 365);
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
