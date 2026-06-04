#include "ui/hex_highlighter.h"
#include <algorithm>
#include <format>

#if __has_include(<cxxabi.h>)
#include <cstdlib>
#include <cxxabi.h>
#define HAS_CXXABI
#endif

#ifdef HAVE_ZYDIS
#include <Zydis/Zydis.h>
#endif

// Helper functions for bounds-safe endian reading
namespace
{

std::string demangle_symbol(const std::string &mangled)
{
#ifdef HAS_CXXABI
	int status = 0;
	char *demangled = abi::__cxa_demangle(mangled.c_str(), nullptr, nullptr, &status);
	if (status == 0 && demangled) {
		std::string result(demangled);
		std::free(demangled);
		return result;
	}

	// Try progressively shorter prefixes to handle template constraints
	// or truncated/cut-off mangled symbols.
	if (mangled.starts_with("_Z")) {
		for (size_t len = mangled.length() - 1; len >= 3; --len) {
			std::string prefix = mangled.substr(0, len);
			status = 0;
			demangled = abi::__cxa_demangle(prefix.c_str(), nullptr, nullptr, &status);
			if (status == 0 && demangled) {
				std::string result(demangled);
				std::free(demangled);
				std::string suffix = mangled.substr(len);
				if (suffix.starts_with('Q')) {
					std::string constraint = suffix.substr(1);
					size_t first_non_digit = 0;
					while (first_non_digit < constraint.length() &&
					       constraint[first_non_digit] >= '0' &&
					       constraint[first_non_digit] <= '9') {
						first_non_digit++;
					}
					if (first_non_digit > 0 && first_non_digit < constraint.length()) {
						constraint = constraint.substr(first_non_digit);
					}
					return std::format("{} [requires {}]", result, constraint);
				}
				return std::format("{} [{}]", result, suffix);
			}
		}
	}
#endif
	return mangled;
}

std::string demangle_section_name(const std::string &name)
{
	std::string result;
	size_t start = 0;
	while (start < name.length()) {
		size_t dot = name.find('.', start);
		std::string part = (dot == std::string::npos) ? name.substr(start) : name.substr(start, dot - start);

		if (part.starts_with("_Z")) {
			result += demangle_symbol(part);
		} else {
			result += part;
		}

		if (dot == std::string::npos) {
			break;
		}
		result += ".";
		start = dot + 1;
	}
	return result;
}

uint16_t read_u16(const std::vector<uint8_t> &data, size_t offset, bool is_lsb)
{
	if (offset + 2 > data.size())
		return 0;
	if (is_lsb) {
		return data[offset] | (data[offset + 1] << 8);
	} else {
		return (data[offset] << 8) | data[offset + 1];
	}
}

uint32_t read_u32(const std::vector<uint8_t> &data, size_t offset, bool is_lsb)
{
	if (offset + 4 > data.size())
		return 0;
	if (is_lsb) {
		return data[offset] | (data[offset + 1] << 8) | (data[offset + 2] << 16) | (data[offset + 3] << 24);
	} else {
		return (data[offset] << 24) | (data[offset + 1] << 16) | (data[offset + 2] << 8) | data[offset + 3];
	}
}

uint64_t read_u64(const std::vector<uint8_t> &data, size_t offset, bool is_lsb)
{
	if (offset + 8 > data.size())
		return 0;
	uint64_t low = read_u32(data, offset, is_lsb);
	uint64_t high = read_u32(data, offset + 4, is_lsb);
	if (is_lsb) {
		return low | (high << 32);
	} else {
		return (low << 32) | high;
	}
}

std::string read_string(const std::vector<uint8_t> &data, size_t offset, size_t max_len)
{
	std::string res;
	for (size_t i = 0; i < max_len; ++i) {
		if (offset + i >= data.size())
			break;
		char c = static_cast<char>(data[offset + i]);
		if (c == '\0')
			break;
		res.push_back(c);
	}
	return res;
}

std::string get_class_desc(uint8_t val)
{
	if (val == 1)
		return "ELFCLASS32 (32-bit)";
	if (val == 2)
		return "ELFCLASS64 (64-bit)";
	return std::format("Invalid ({})", val);
}

std::string get_data_desc(uint8_t val)
{
	if (val == 1)
		return "ELFDATA2LSB (Little Endian)";
	if (val == 2)
		return "ELFDATA2MSB (Big Endian)";
	return std::format("Invalid ({})", val);
}

std::string get_type_desc(uint16_t val)
{
	switch (val) {
		case 0:
			return "ET_NONE (None)";
		case 1:
			return "ET_REL (Relocatable)";
		case 2:
			return "ET_EXEC (Executable)";
		case 3:
			return "ET_DYN (Shared object)";
		case 4:
			return "ET_CORE (Core file)";
		default:
			return std::format("Processor/OS specific (0x{:04X})", val);
	}
}

std::string get_machine_desc(uint16_t val)
{
	switch (val) {
		case 0:
			return "EM_NONE (None)";
		case 2:
			return "EM_SPARC (SPARC)";
		case 3:
			return "EM_386 (Intel 80386)";
		case 4:
			return "EM_68K (Motorola 68000)";
		case 8:
			return "EM_MIPS (MIPS)";
		case 20:
			return "EM_PPC (PowerPC)";
		case 21:
			return "EM_PPC64 (PowerPC 64-bit)";
		case 40:
			return "EM_ARM (ARM)";
		case 50:
			return "EM_IA_64 (Itanium)";
		case 62:
			return "EM_X86_64 (AMD x86-64)";
		default:
			return std::format("Other machine (0x{:04X})", val);
	}
}

std::string get_osabi_desc(uint8_t val)
{
	switch (val) {
		case 0:
			return "ELFOSABI_NONE / SYSV (UNIX System V)";
		case 1:
			return "ELFOSABI_HPUX (HP-UX)";
		case 2:
			return "ELFOSABI_NETBSD (NetBSD)";
		case 3:
			return "ELFOSABI_LINUX (Linux)";
		case 6:
			return "ELFOSABI_SOLARIS (Solaris)";
		case 8:
			return "ELFOSABI_FREEBSD (FreeBSD)";
		case 9:
			return "ELFOSABI_ARM (ARM ABI)";
		case 97:
			return "ELFOSABI_STANDALONE (Standalone)";
		default:
			return std::format("Other OS ABI ({})", val);
	}
}

std::string get_phdr_type_desc(uint32_t val)
{
	switch (val) {
		case 0:
			return "PT_NULL (Unused)";
		case 1:
			return "PT_LOAD (Loadable Segment)";
		case 2:
			return "PT_DYNAMIC (Dynamic Linking Info)";
		case 3:
			return "PT_INTERP (Interpreter Path)";
		case 4:
			return "PT_NOTE (Auxiliary Notes)";
		case 5:
			return "PT_SHLIB (Reserved)";
		case 6:
			return "PT_PHDR (Program Header Table Location)";
		case 0x6474e551:
			return "PT_GNU_STACK (Stack Execution Control)";
		default:
			return std::format("Processor/OS specific (0x{:08X})", val);
	}
}

std::string get_shdr_type_desc(uint32_t val)
{
	switch (val) {
		case 0:
			return "SHT_NULL (Inactive)";
		case 1:
			return "SHT_PROGBITS (Program/Code/Data)";
		case 2:
			return "SHT_SYMTAB (Symbol Table)";
		case 3:
			return "SHT_STRTAB (String Table)";
		case 4:
			return "SHT_RELA (Relocation with Addends)";
		case 5:
			return "SHT_HASH (Symbol Hash)";
		case 6:
			return "SHT_DYNAMIC (Dynamic Linking Info)";
		case 7:
			return "SHT_NOTE (Notes)";
		case 8:
			return "SHT_NOBITS (Uninitialized/BSS)";
		case 9:
			return "SHT_REL (Relocation without Addends)";
		case 11:
			return "SHT_DYNSYM (Dynamic Link Symbols)";
		default:
			return std::format("Processor/OS/User specific (0x{:08X})", val);
	}
}

std::string get_shdr_flags_desc(uint64_t val)
{
	std::string res;
	if (val & 0x1)
		res += "W(Write) ";
	if (val & 0x2)
		res += "A(Alloc) ";
	if (val & 0x4)
		res += "X(Exec) ";
	if (res.empty())
		res = "None";
	else if (res.back() == ' ')
		res.pop_back();
	return std::format("0x{:X} ({})", val, res);
}

} // namespace

bool elf_hex_highlighter::can_handle(const std::vector<uint8_t> &data) const
{
	if (data.size() < 64)
		return false;
	return data[0] == 0x7F && data[1] == 'E' && data[2] == 'L' && data[3] == 'F';
}

bool elf_hex_highlighter::parse(const std::vector<uint8_t> &data)
{
	parsed_successfully_ = false;
	sections_.clear();
	symbols_.clear();

	if (!can_handle(data))
		return false;

	uint8_t elf_class = data[4];
	if (elf_class != 1 && elf_class != 2)
		return false;

	uint8_t elf_data = data[5];
	if (elf_data != 1 && elf_data != 2)
		return false;

	header_.is_64 = (elf_class == 2);
	header_.is_lsb = (elf_data == 1);

	// Read Ehdr fields bounds-safely
	bool lsb = header_.is_lsb;
	header_.e_machine = read_u16(data, 18, lsb);
	if (header_.is_64) {
		header_.e_entry = read_u64(data, 24, lsb);
		header_.e_phoff = read_u64(data, 32, lsb);
		header_.e_shoff = read_u64(data, 40, lsb);
		header_.e_ehsize = read_u16(data, 52, lsb);
		header_.e_phentsize = read_u16(data, 54, lsb);
		header_.e_phnum = read_u16(data, 56, lsb);
		header_.e_shentsize = read_u16(data, 58, lsb);
		header_.e_shnum = read_u16(data, 60, lsb);
		header_.e_shstrndx = read_u16(data, 62, lsb);
	} else {
		header_.e_entry = read_u32(data, 24, lsb);
		header_.e_phoff = read_u32(data, 28, lsb);
		header_.e_shoff = read_u32(data, 32, lsb);
		header_.e_ehsize = read_u16(data, 40, lsb);
		header_.e_phentsize = read_u16(data, 42, lsb);
		header_.e_phnum = read_u16(data, 44, lsb);
		header_.e_shentsize = read_u16(data, 46, lsb);
		header_.e_shnum = read_u16(data, 48, lsb);
		header_.e_shstrndx = read_u16(data, 50, lsb);
	}

	// Parse sections bounds-safely
	if (header_.e_shoff > 0 && header_.e_shnum > 0 && header_.e_shentsize >= (header_.is_64 ? 64 : 40)) {
		// First find string table offset
		uint64_t strtab_offset = 0;
		uint64_t strtab_size = 0;
		if (header_.e_shstrndx < header_.e_shnum) {
			size_t strtab_entry_start = header_.e_shoff + header_.e_shstrndx * header_.e_shentsize;
			if (header_.is_64) {
				strtab_offset = read_u64(data, strtab_entry_start + 24, lsb);
				strtab_size = read_u64(data, strtab_entry_start + 32, lsb);
			} else {
				strtab_offset = read_u32(data, strtab_entry_start + 16, lsb);
				strtab_size = read_u32(data, strtab_entry_start + 20, lsb);
			}
		}

		for (size_t i = 1; i < header_.e_shnum; ++i) {
			size_t entry_start = header_.e_shoff + i * header_.e_shentsize;
			if (entry_start + header_.e_shentsize > data.size())
				break;

			parsed_section sec;
			sec.index = i;

			uint32_t name_offset = read_u32(data, entry_start, lsb);
			if (strtab_offset > 0 && name_offset < strtab_size) {
				sec.name = read_string(data, strtab_offset + name_offset, 128);
			} else {
				sec.name = std::format("sect_{}", i);
			}

			if (header_.is_64) {
				sec.type_val = read_u32(data, entry_start + 4, lsb);
				sec.offset = read_u64(data, entry_start + 24, lsb);
				sec.size = read_u64(data, entry_start + 32, lsb);
			} else {
				sec.type_val = read_u32(data, entry_start + 4, lsb);
				sec.offset = read_u32(data, entry_start + 16, lsb);
				sec.size = read_u32(data, entry_start + 20, lsb);
			}

			// Map semantic type based on section name/type
			if (sec.type_val != 8) { // SHT_NOBITS (occupies no file space)
				if (sec.name == ".text" || sec.name.starts_with(".text")) {
					sec.semantic = hex_semantic_type::code_section;
				} else if (sec.name == ".data" || sec.name == ".bss" || sec.name == ".tbss" || sec.name == ".tdata") {
					sec.semantic = hex_semantic_type::data_section;
				} else if (sec.name == ".rodata" || sec.name == ".eh_frame" || sec.name == ".dynstr" ||
					   sec.name == ".dynsym") {
					sec.semantic = hex_semantic_type::rodata_section;
				} else if (sec.name == ".symtab" || sec.name == ".strtab" || sec.name == ".shstrtab") {
					sec.semantic = hex_semantic_type::symtab_section;
				} else {
					sec.semantic = hex_semantic_type::rodata_section; // Default constant section
				}
				sec.name = demangle_section_name(sec.name);
				sections_.push_back(sec);
			}
		}
	}

	// Parse symbol table bounds-safely if SHT exists
	if (header_.e_shoff > 0 && header_.e_shnum > 0 && header_.e_shentsize >= (header_.is_64 ? 64 : 40)) {
		uint64_t symtab_offset = 0;
		uint64_t symtab_size = 0;
		uint64_t symtab_entsize = 0;
		uint32_t symtab_link = 0;

		for (size_t i = 1; i < header_.e_shnum; ++i) {
			size_t entry_start = header_.e_shoff + i * header_.e_shentsize;
			if (entry_start + header_.e_shentsize > data.size())
				break;
			uint32_t type = read_u32(data, entry_start + 4, lsb);
			if (type == 2) { // SHT_SYMTAB
				if (header_.is_64) {
					symtab_offset = read_u64(data, entry_start + 24, lsb);
					symtab_size = read_u64(data, entry_start + 32, lsb);
					symtab_link = read_u32(data, entry_start + 40, lsb);
					symtab_entsize = read_u64(data, entry_start + 56, lsb);
				} else {
					symtab_offset = read_u32(data, entry_start + 16, lsb);
					symtab_size = read_u32(data, entry_start + 20, lsb);
					symtab_link = read_u32(data, entry_start + 24, lsb);
					symtab_entsize = read_u32(data, entry_start + 36, lsb);
				}
				break;
			}
		}

		uint64_t symstr_offset = 0;
		uint64_t symstr_size = 0;
		if (symtab_offset > 0 && symtab_size > 0 && symtab_entsize > 0 && symtab_link < header_.e_shnum) {
			size_t entry_start = header_.e_shoff + symtab_link * header_.e_shentsize;
			if (entry_start + header_.e_shentsize <= data.size()) {
				if (header_.is_64) {
					symstr_offset = read_u64(data, entry_start + 24, lsb);
					symstr_size = read_u64(data, entry_start + 32, lsb);
				} else {
					symstr_offset = read_u32(data, entry_start + 16, lsb);
					symstr_size = read_u32(data, entry_start + 20, lsb);
				}
			}

			size_t sym_count = symtab_size / symtab_entsize;
			for (size_t j = 0; j < sym_count; ++j) {
				size_t sym_start = symtab_offset + j * symtab_entsize;
				if (sym_start + symtab_entsize > data.size())
					break;

				uint32_t st_name = 0;
				uint8_t st_info = 0;
				uint16_t st_shndx = 0;
				uint64_t st_value = 0;
				uint64_t st_size = 0;

				if (header_.is_64) {
					st_name = read_u32(data, sym_start, lsb);
					st_info = data[sym_start + 4];
					st_shndx = read_u16(data, sym_start + 6, lsb);
					st_value = read_u64(data, sym_start + 8, lsb);
					st_size = read_u64(data, sym_start + 16, lsb);
				} else {
					st_name = read_u32(data, sym_start, lsb);
					st_value = read_u32(data, sym_start + 4, lsb);
					st_size = read_u32(data, sym_start + 8, lsb);
					st_info = data[sym_start + 12];
					st_shndx = read_u16(data, sym_start + 14, lsb);
				}

				uint8_t sym_type = st_info & 0xF;
				if (sym_type == 2 && st_size > 0 && st_shndx < header_.e_shnum) {
					uint64_t sym_file_offset = 0;
					bool resolved = false;

					for (const auto &sec : sections_) {
						if (sec.index == st_shndx) {
							uint16_t e_type = read_u16(data, 16, lsb);
							if (e_type == 1) { // ET_REL
								sym_file_offset = sec.offset + st_value;
							} else {
								size_t sec_entry = header_.e_shoff + sec.index * header_.e_shentsize;
								uint64_t sh_addr = 0;
								if (header_.is_64) {
									sh_addr = read_u64(data, sec_entry + 16, lsb);
								} else {
									sh_addr = read_u32(data, sec_entry + 12, lsb);
								}
								if (st_value >= sh_addr && st_value < sh_addr + sec.size) {
									sym_file_offset = sec.offset + (st_value - sh_addr);
								} else {
									continue;
								}
							}
							resolved = true;
							break;
						}
					}

					if (resolved) {
						parsed_symbol sym;
						if (symstr_offset > 0 && st_name < symstr_size) {
							sym.name = read_string(data, symstr_offset + st_name, 128);
							sym.name = demangle_section_name(sym.name);
						} else {
							sym.name = std::format("sym_func_{:X}", st_value);
						}
						sym.offset = sym_file_offset;
						sym.size = st_size;
						symbols_.push_back(sym);
					}
				}
			}
		}

		std::sort(symbols_.begin(), symbols_.end(), [](const parsed_symbol &a, const parsed_symbol &b) {
			return a.offset < b.offset;
		});
	}

	parsed_successfully_ = true;
	return true;
}

highlight_info elf_hex_highlighter::get_info(const std::vector<uint8_t> &data, size_t offset) const
{
	if (!parsed_successfully_)
		return {};

	bool lsb = header_.is_lsb;

	// 1. Magic
	if (offset < 4) {
		return {hex_semantic_type::magic, std::format("ELF Magic Sign: '\\x7fELF'")};
	}

	// 2. Identification details
	if (offset >= 4 && offset < 16) {
		if (offset == 4) {
			return {hex_semantic_type::file_header, std::format("ELF Ehdr: e_ident[EI_CLASS] = {}", get_class_desc(data[4]))};
		}
		if (offset == 5) {
			return {hex_semantic_type::file_header, std::format("ELF Ehdr: e_ident[EI_DATA] = {}", get_data_desc(data[5]))};
		}
		if (offset == 6) {
			return {hex_semantic_type::file_header, std::format("ELF Ehdr: e_ident[EI_VERSION] = {}", data[6])};
		}
		if (offset == 7) {
			return {hex_semantic_type::file_header, std::format("ELF Ehdr: e_ident[EI_OSABI] = {}", get_osabi_desc(data[7]))};
		}
		if (offset == 8) {
			return {hex_semantic_type::file_header, std::format("ELF Ehdr: e_ident[EI_ABIVERSION] = {}", data[8])};
		}
		return {hex_semantic_type::file_header, std::format("ELF Ehdr: e_ident padding [{}]", offset - 9)};
	}

	// 3. Ehdr remaining fields
	if (offset < header_.e_ehsize) {
		size_t relative = offset;
		if (header_.is_64) {
			if (relative >= 16 && relative < 18) {
				uint16_t type = read_u16(data, 16, lsb);
				return {hex_semantic_type::file_header, std::format("ELF Ehdr: e_type = {}", get_type_desc(type))};
			}
			if (relative >= 18 && relative < 20) {
				uint16_t mach = read_u16(data, 18, lsb);
				return {hex_semantic_type::file_header, std::format("ELF Ehdr: e_machine = {}", get_machine_desc(mach))};
			}
			if (relative >= 20 && relative < 24) {
				uint32_t ver = read_u32(data, 20, lsb);
				return {hex_semantic_type::file_header, std::format("ELF Ehdr: e_version = {}", ver)};
			}
			if (relative >= 24 && relative < 32) {
				uint64_t ent = read_u64(data, 24, lsb);
				return {hex_semantic_type::file_header, std::format("ELF Ehdr: e_entry = 0x{:08X}", ent)};
			}
			if (relative >= 32 && relative < 40) {
				uint64_t phoff = read_u64(data, 32, lsb);
				return {hex_semantic_type::file_header, std::format("ELF Ehdr: e_phoff = 0x{:X}", phoff)};
			}
			if (relative >= 40 && relative < 48) {
				uint64_t shoff = read_u64(data, 40, lsb);
				return {hex_semantic_type::file_header, std::format("ELF Ehdr: e_shoff = 0x{:X}", shoff)};
			}
			if (relative >= 48 && relative < 52) {
				uint32_t flags = read_u32(data, 48, lsb);
				return {hex_semantic_type::file_header, std::format("ELF Ehdr: e_flags = 0x{:08X}", flags)};
			}
			if (relative >= 52 && relative < 54) {
				return {hex_semantic_type::file_header, std::format("ELF Ehdr: e_ehsize = {} bytes", header_.e_ehsize)};
			}
			if (relative >= 54 && relative < 56) {
				return {hex_semantic_type::file_header,
					std::format("ELF Ehdr: e_phentsize = {} bytes", header_.e_phentsize)};
			}
			if (relative >= 56 && relative < 58) {
				return {hex_semantic_type::file_header, std::format("ELF Ehdr: e_phnum = {}", header_.e_phnum)};
			}
			if (relative >= 58 && relative < 60) {
				return {hex_semantic_type::file_header,
					std::format("ELF Ehdr: e_shentsize = {} bytes", header_.e_shentsize)};
			}
			if (relative >= 60 && relative < 62) {
				return {hex_semantic_type::file_header, std::format("ELF Ehdr: e_shnum = {}", header_.e_shnum)};
			}
			if (relative >= 62 && relative < 64) {
				return {hex_semantic_type::file_header, std::format("ELF Ehdr: e_shstrndx = {}", header_.e_shstrndx)};
			}
		} else {
			// 32-bit Ehdr
			if (relative >= 16 && relative < 18) {
				uint16_t type = read_u16(data, 16, lsb);
				return {hex_semantic_type::file_header, std::format("ELF Ehdr: e_type = {}", get_type_desc(type))};
			}
			if (relative >= 18 && relative < 20) {
				uint16_t mach = read_u16(data, 18, lsb);
				return {hex_semantic_type::file_header, std::format("ELF Ehdr: e_machine = {}", get_machine_desc(mach))};
			}
			if (relative >= 20 && relative < 24) {
				uint32_t ver = read_u32(data, 20, lsb);
				return {hex_semantic_type::file_header, std::format("ELF Ehdr: e_version = {}", ver)};
			}
			if (relative >= 24 && relative < 28) {
				uint32_t ent = read_u32(data, 24, lsb);
				return {hex_semantic_type::file_header, std::format("ELF Ehdr: e_entry = 0x{:08X}", ent)};
			}
			if (relative >= 28 && relative < 32) {
				uint32_t phoff = read_u32(data, 28, lsb);
				return {hex_semantic_type::file_header, std::format("ELF Ehdr: e_phoff = 0x{:X}", phoff)};
			}
			if (relative >= 32 && relative < 36) {
				uint32_t shoff = read_u32(data, 32, lsb);
				return {hex_semantic_type::file_header, std::format("ELF Ehdr: e_shoff = 0x{:X}", shoff)};
			}
			if (relative >= 36 && relative < 40) {
				uint32_t flags = read_u32(data, 36, lsb);
				return {hex_semantic_type::file_header, std::format("ELF Ehdr: e_flags = 0x{:08X}", flags)};
			}
			if (relative >= 40 && relative < 42) {
				return {hex_semantic_type::file_header, std::format("ELF Ehdr: e_ehsize = {} bytes", header_.e_ehsize)};
			}
			if (relative >= 42 && relative < 44) {
				return {hex_semantic_type::file_header,
					std::format("ELF Ehdr: e_phentsize = {} bytes", header_.e_phentsize)};
			}
			if (relative >= 44 && relative < 46) {
				return {hex_semantic_type::file_header, std::format("ELF Ehdr: e_phnum = {}", header_.e_phnum)};
			}
			if (relative >= 46 && relative < 48) {
				return {hex_semantic_type::file_header,
					std::format("ELF Ehdr: e_shentsize = {} bytes", header_.e_shentsize)};
			}
			if (relative >= 48 && relative < 50) {
				return {hex_semantic_type::file_header, std::format("ELF Ehdr: e_shnum = {}", header_.e_shnum)};
			}
			if (relative >= 50 && relative < 52) {
				return {hex_semantic_type::file_header, std::format("ELF Ehdr: e_shstrndx = {}", header_.e_shstrndx)};
			}
		}
		return {hex_semantic_type::file_header, "ELF Ehdr Header"};
	}

	// 4. Program Header Table
	if (header_.e_phoff > 0 && header_.e_phnum > 0) {
		size_t ph_table_end = header_.e_phoff + (header_.e_phnum * header_.e_phentsize);
		if (offset >= header_.e_phoff && offset < ph_table_end) {
			size_t entry_idx = (offset - header_.e_phoff) / header_.e_phentsize;
			size_t field_offset = (offset - header_.e_phoff) % header_.e_phentsize;
			size_t entry_start = header_.e_phoff + entry_idx * header_.e_phentsize;

			if (header_.is_64) {
				if (field_offset < 4) {
					uint32_t type = read_u32(data, entry_start, lsb);
					return {hex_semantic_type::prog_header,
						std::format("ELF Phdr[{}]: p_type = {}", entry_idx, get_phdr_type_desc(type))};
				}
				if (field_offset >= 4 && field_offset < 8) {
					uint32_t flags = read_u32(data, entry_start + 4, lsb);
					std::string f_str;
					if (flags & 1)
						f_str += "X ";
					if (flags & 2)
						f_str += "W ";
					if (flags & 4)
						f_str += "R ";
					if (f_str.empty())
						f_str = "None";
					else
						f_str.pop_back();
					return {hex_semantic_type::prog_header,
						std::format("ELF Phdr[{}]: p_flags = 0x{:X} ({})", entry_idx, flags, f_str)};
				}
				if (field_offset >= 8 && field_offset < 16) {
					uint64_t val = read_u64(data, entry_start + 8, lsb);
					return {hex_semantic_type::prog_header,
						std::format("ELF Phdr[{}]: p_offset = 0x{:X}", entry_idx, val)};
				}
				if (field_offset >= 16 && field_offset < 24) {
					uint64_t val = read_u64(data, entry_start + 16, lsb);
					return {hex_semantic_type::prog_header,
						std::format("ELF Phdr[{}]: p_vaddr = 0x{:X}", entry_idx, val)};
				}
				if (field_offset >= 24 && field_offset < 32) {
					uint64_t val = read_u64(data, entry_start + 24, lsb);
					return {hex_semantic_type::prog_header,
						std::format("ELF Phdr[{}]: p_paddr = 0x{:X}", entry_idx, val)};
				}
				if (field_offset >= 32 && field_offset < 40) {
					uint64_t val = read_u64(data, entry_start + 32, lsb);
					return {hex_semantic_type::prog_header,
						std::format("ELF Phdr[{}]: p_filesz = {} bytes (0x{:X})", entry_idx, val, val)};
				}
				if (field_offset >= 40 && field_offset < 48) {
					uint64_t val = read_u64(data, entry_start + 40, lsb);
					return {hex_semantic_type::prog_header,
						std::format("ELF Phdr[{}]: p_memsz = {} bytes (0x{:X})", entry_idx, val, val)};
				}
				if (field_offset >= 48 && field_offset < 56) {
					uint64_t val = read_u64(data, entry_start + 48, lsb);
					return {hex_semantic_type::prog_header,
						std::format("ELF Phdr[{}]: p_align = 0x{:X}", entry_idx, val)};
				}
			} else {
				// 32-bit Phdr
				if (field_offset < 4) {
					uint32_t type = read_u32(data, entry_start, lsb);
					return {hex_semantic_type::prog_header,
						std::format("ELF Phdr[{}]: p_type = {}", entry_idx, get_phdr_type_desc(type))};
				}
				if (field_offset >= 4 && field_offset < 8) {
					uint32_t val = read_u32(data, entry_start + 4, lsb);
					return {hex_semantic_type::prog_header,
						std::format("ELF Phdr[{}]: p_offset = 0x{:X}", entry_idx, val)};
				}
				if (field_offset >= 8 && field_offset < 12) {
					uint32_t val = read_u32(data, entry_start + 8, lsb);
					return {hex_semantic_type::prog_header,
						std::format("ELF Phdr[{}]: p_vaddr = 0x{:X}", entry_idx, val)};
				}
				if (field_offset >= 12 && field_offset < 16) {
					uint32_t val = read_u32(data, entry_start + 12, lsb);
					return {hex_semantic_type::prog_header,
						std::format("ELF Phdr[{}]: p_paddr = 0x{:X}", entry_idx, val)};
				}
				if (field_offset >= 16 && field_offset < 20) {
					uint32_t val = read_u32(data, entry_start + 16, lsb);
					return {hex_semantic_type::prog_header,
						std::format("ELF Phdr[{}]: p_filesz = {} bytes (0x{:X})", entry_idx, val, val)};
				}
				if (field_offset >= 20 && field_offset < 24) {
					uint32_t val = read_u32(data, entry_start + 20, lsb);
					return {hex_semantic_type::prog_header,
						std::format("ELF Phdr[{}]: p_memsz = {} bytes (0x{:X})", entry_idx, val, val)};
				}
				if (field_offset >= 24 && field_offset < 28) {
					uint32_t flags = read_u32(data, entry_start + 24, lsb);
					std::string f_str;
					if (flags & 1)
						f_str += "X ";
					if (flags & 2)
						f_str += "W ";
					if (flags & 4)
						f_str += "R ";
					if (f_str.empty())
						f_str = "None";
					else
						f_str.pop_back();
					return {hex_semantic_type::prog_header,
						std::format("ELF Phdr[{}]: p_flags = 0x{:X} ({})", entry_idx, flags, f_str)};
				}
				if (field_offset >= 28 && field_offset < 32) {
					uint32_t val = read_u32(data, entry_start + 28, lsb);
					return {hex_semantic_type::prog_header,
						std::format("ELF Phdr[{}]: p_align = 0x{:X}", entry_idx, val)};
				}
			}
			return {hex_semantic_type::prog_header, std::format("ELF Phdr[{}] Segment entry", entry_idx)};
		}
	}

	// 5. Section Header Table
	if (header_.e_shoff > 0 && header_.e_shnum > 0) {
		size_t sh_table_end = header_.e_shoff + (header_.e_shnum * header_.e_shentsize);
		if (offset >= header_.e_shoff && offset < sh_table_end) {
			size_t entry_idx = (offset - header_.e_shoff) / header_.e_shentsize;
			size_t field_offset = (offset - header_.e_shoff) % header_.e_shentsize;
			size_t entry_start = header_.e_shoff + entry_idx * header_.e_shentsize;

			// Find name
			std::string sec_name_label;
			for (const auto &sec : sections_) {
				if (sec.index == entry_idx) {
					sec_name_label = std::format("(\"{}\")", sec.name);
					break;
				}
			}

			if (header_.is_64) {
				if (field_offset < 4) {
					uint32_t val = read_u32(data, entry_start, lsb);
					return {
					    hex_semantic_type::sect_header,
					    std::format("ELF Shdr[{}]{}: sh_name = 0x{:X} String offset", entry_idx, sec_name_label, val)};
				}
				if (field_offset >= 4 && field_offset < 8) {
					uint32_t type = read_u32(data, entry_start + 4, lsb);
					return {hex_semantic_type::sect_header, std::format("ELF Shdr[{}]{}: sh_type = {}", entry_idx,
											    sec_name_label, get_shdr_type_desc(type))};
				}
				if (field_offset >= 8 && field_offset < 16) {
					uint64_t val = read_u64(data, entry_start + 8, lsb);
					return {hex_semantic_type::sect_header, std::format("ELF Shdr[{}]{}: sh_flags = {}", entry_idx,
											    sec_name_label, get_shdr_flags_desc(val))};
				}
				if (field_offset >= 16 && field_offset < 24) {
					uint64_t val = read_u64(data, entry_start + 16, lsb);
					return {hex_semantic_type::sect_header,
						std::format("ELF Shdr[{}]{}: sh_addr = 0x{:08X}", entry_idx, sec_name_label, val)};
				}
				if (field_offset >= 24 && field_offset < 32) {
					uint64_t val = read_u64(data, entry_start + 24, lsb);
					return {hex_semantic_type::sect_header,
						std::format("ELF Shdr[{}]{}: sh_offset = 0x{:X}", entry_idx, sec_name_label, val)};
				}
				if (field_offset >= 32 && field_offset < 40) {
					uint64_t val = read_u64(data, entry_start + 32, lsb);
					return {hex_semantic_type::sect_header, std::format("ELF Shdr[{}]{}: sh_size = {} bytes (0x{:X})",
											    entry_idx, sec_name_label, val, val)};
				}
				if (field_offset >= 40 && field_offset < 44) {
					uint32_t val = read_u32(data, entry_start + 40, lsb);
					return {hex_semantic_type::sect_header,
						std::format("ELF Shdr[{}]{}: sh_link = {}", entry_idx, sec_name_label, val)};
				}
				if (field_offset >= 44 && field_offset < 48) {
					uint32_t val = read_u32(data, entry_start + 44, lsb);
					return {hex_semantic_type::sect_header,
						std::format("ELF Shdr[{}]{}: sh_info = {}", entry_idx, sec_name_label, val)};
				}
				if (field_offset >= 48 && field_offset < 56) {
					uint64_t val = read_u64(data, entry_start + 48, lsb);
					return {hex_semantic_type::sect_header,
						std::format("ELF Shdr[{}]{}: sh_addralign = {}", entry_idx, sec_name_label, val)};
				}
				if (field_offset >= 56 && field_offset < 64) {
					uint64_t val = read_u64(data, entry_start + 56, lsb);
					return {hex_semantic_type::sect_header,
						std::format("ELF Shdr[{}]{}: sh_entsize = {} bytes", entry_idx, sec_name_label, val)};
				}
			} else {
				// 32-bit Shdr
				if (field_offset < 4) {
					uint32_t val = read_u32(data, entry_start, lsb);
					return {
					    hex_semantic_type::sect_header,
					    std::format("ELF Shdr[{}]{}: sh_name = 0x{:X} String offset", entry_idx, sec_name_label, val)};
				}
				if (field_offset >= 4 && field_offset < 8) {
					uint32_t type = read_u32(data, entry_start + 4, lsb);
					return {hex_semantic_type::sect_header, std::format("ELF Shdr[{}]{}: sh_type = {}", entry_idx,
											    sec_name_label, get_shdr_type_desc(type))};
				}
				if (field_offset >= 8 && field_offset < 12) {
					uint32_t val = read_u32(data, entry_start + 8, lsb);
					return {hex_semantic_type::sect_header, std::format("ELF Shdr[{}]{}: sh_flags = {}", entry_idx,
											    sec_name_label, get_shdr_flags_desc(val))};
				}
				if (field_offset >= 12 && field_offset < 16) {
					uint32_t val = read_u32(data, entry_start + 12, lsb);
					return {hex_semantic_type::sect_header,
						std::format("ELF Shdr[{}]{}: sh_addr = 0x{:08X}", entry_idx, sec_name_label, val)};
				}
				if (field_offset >= 16 && field_offset < 20) {
					uint32_t val = read_u32(data, entry_start + 16, lsb);
					return {hex_semantic_type::sect_header,
						std::format("ELF Shdr[{}]{}: sh_offset = 0x{:X}", entry_idx, sec_name_label, val)};
				}
				if (field_offset >= 20 && field_offset < 24) {
					uint32_t val = read_u32(data, entry_start + 20, lsb);
					return {hex_semantic_type::sect_header, std::format("ELF Shdr[{}]{}: sh_size = {} bytes (0x{:X})",
											    entry_idx, sec_name_label, val, val)};
				}
				if (field_offset >= 24 && field_offset < 28) {
					uint32_t val = read_u32(data, entry_start + 24, lsb);
					return {hex_semantic_type::sect_header,
						std::format("ELF Shdr[{}]{}: sh_link = {}", entry_idx, sec_name_label, val)};
				}
				if (field_offset >= 28 && field_offset < 32) {
					uint32_t val = read_u32(data, entry_start + 28, lsb);
					return {hex_semantic_type::sect_header,
						std::format("ELF Shdr[{}]{}: sh_info = {}", entry_idx, sec_name_label, val)};
				}
				if (field_offset >= 32 && field_offset < 36) {
					uint32_t val = read_u32(data, entry_start + 32, lsb);
					return {hex_semantic_type::sect_header,
						std::format("ELF Shdr[{}]{}: sh_addralign = {}", entry_idx, sec_name_label, val)};
				}
				if (field_offset >= 36 && field_offset < 40) {
					uint32_t val = read_u32(data, entry_start + 36, lsb);
					return {hex_semantic_type::sect_header,
						std::format("ELF Shdr[{}]{}: sh_entsize = {} bytes", entry_idx, sec_name_label, val)};
				}
			}
			return {hex_semantic_type::sect_header, std::format("ELF Shdr[{}] Section entry", entry_idx)};
		}
	}

	// 6. Section Ranges (linear scan for matches)
	for (const auto &sec : sections_) {
		if (offset >= sec.offset && offset < sec.offset + sec.size) {
			size_t relative = offset - sec.offset;
#ifdef HAVE_ZYDIS
			if (sec.semantic == hex_semantic_type::code_section && (header_.e_machine == 3 || header_.e_machine == 62)) {
				ZydisMachineMode machine_mode = (header_.e_machine == 62) ? ZYDIS_MACHINE_MODE_LONG_64 : ZYDIS_MACHINE_MODE_LEGACY_32;

				const parsed_symbol *found_sym = nullptr;
				for (const auto &sym : symbols_) {
					if (offset >= sym.offset && offset < sym.offset + sym.size) {
						found_sym = &sym;
						break;
					}
				}

				size_t start_offset = found_sym ? found_sym->offset : sec.offset;
				size_t limit_offset = found_sym ? (found_sym->offset + found_sym->size) : (sec.offset + sec.size);

				if (!found_sym && offset - start_offset > 65536) {
					start_offset = offset - (offset % 16);
				}

				if (limit_offset > data.size()) {
					limit_offset = data.size();
				}

				size_t inst_start = start_offset;
				size_t sec_entry = header_.e_shoff + sec.index * header_.e_shentsize;
				uint64_t sh_addr = 0;
				if (header_.is_64) {
					sh_addr = read_u64(data, sec_entry + 16, header_.is_lsb);
				} else {
					sh_addr = read_u32(data, sec_entry + 12, header_.is_lsb);
				}
				ZyanU64 runtime_address = sh_addr + (inst_start - sec.offset);

				while (inst_start < limit_offset && inst_start <= offset) {
					ZydisDisassembledInstruction instruction;
					if (!ZYAN_SUCCESS(ZydisDisassembleATT(
						machine_mode,
						runtime_address,
						data.data() + inst_start,
						data.size() - inst_start,
						&instruction
					))) {
						break;
					}

					size_t inst_len = instruction.info.length;
					if (inst_len == 0) {
						break;
					}

					if (offset >= inst_start && offset < inst_start + inst_len) {
						std::string symbol_ctx;
						if (found_sym) {
							symbol_ctx = std::format(" (in {} + 0x{:X})", found_sym->name, offset - found_sym->offset);
						}
						highlight_info info;
						info.type = sec.semantic;
						info.description = std::format("{}{} | ELF Sec \"{}\": type = {}, offset = 0x{:X} (+{})",
										  instruction.text, symbol_ctx, sec.name,
										  get_shdr_type_desc(sec.type_val), sec.offset, relative);
						info.range_start = inst_start;
						info.range_size = inst_len;
						return info;
					}

					inst_start += inst_len;
					runtime_address += inst_len;
				}
			}
#endif
			return {sec.semantic, std::format("ELF Sec \"{}\": type = {}, offset = 0x{:X} (+{})", sec.name,
							  get_shdr_type_desc(sec.type_val), sec.offset, relative)};
		}
	}

	return {hex_semantic_type::normal, ""};
}

hex_highlighter_registry &hex_highlighter_registry::get_instance()
{
	static hex_highlighter_registry inst;
	return inst;
}

hex_highlighter_registry::hex_highlighter_registry()
{
	// Register ELF highlighter
	highlighters_.push_back(std::make_shared<elf_hex_highlighter>());
}

std::shared_ptr<hex_highlighter> hex_highlighter_registry::detect_highlighter(const std::vector<uint8_t> &data) const
{
	for (const auto &hl : highlighters_) {
		if (hl->can_handle(data)) {
			return hl;
		}
	}
	return nullptr;
}

size_t elf_hex_highlighter::get_next_symbol_offset(size_t current_offset) const
{
	for (const auto &sym : symbols_) {
		if (sym.offset > current_offset) {
			return sym.offset;
		}
	}
	return current_offset;
}
