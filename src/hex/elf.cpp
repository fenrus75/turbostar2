#include "hex/elf.h"
#include "mime.h"
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

uint16_t read_u16(std::span<const uint8_t> data, size_t offset, bool is_lsb)
{
	if (offset > data.size() || 2 > data.size() - offset)
		return 0;
	if (is_lsb) {
		return data[offset] | (data[offset + 1] << 8);
	} else {
		return (data[offset] << 8) | data[offset + 1];
	}
}

uint32_t read_u32(std::span<const uint8_t> data, size_t offset, bool is_lsb)
{
	if (offset > data.size() || 4 > data.size() - offset)
		return 0;
	if (is_lsb) {
		return static_cast<uint32_t>(data[offset]) |
		       (static_cast<uint32_t>(data[offset + 1]) << 8) |
		       (static_cast<uint32_t>(data[offset + 2]) << 16) |
		       (static_cast<uint32_t>(data[offset + 3]) << 24);
	} else {
		return (static_cast<uint32_t>(data[offset]) << 24) |
		       (static_cast<uint32_t>(data[offset + 1]) << 16) |
		       (static_cast<uint32_t>(data[offset + 2]) << 8) |
		       static_cast<uint32_t>(data[offset + 3]);
	}
}

uint64_t read_u64(std::span<const uint8_t> data, size_t offset, bool is_lsb)
{
	if (offset > data.size() || 8 > data.size() - offset)
		return 0;
	uint64_t low = read_u32(data, offset, is_lsb);
	uint64_t high = read_u32(data, offset + 4, is_lsb);
	if (is_lsb) {
		return low | (high << 32);
	} else {
		return (low << 32) | high;
	}
}

std::string read_string(std::span<const uint8_t> data, size_t offset, size_t max_len)
{
	std::string res;
	if (offset >= data.size())
		return res;
	size_t count = std::min(max_len, data.size() - offset);
	for (size_t i = 0; i < count; ++i) {
		char c = static_cast<char>(data[offset + i]);
		if (c == '\0')
			break;
		res.push_back((c >= 32 && c <= 126 && c != '|' && c != '`') ? c : '?');
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

std::string shorten_hex_numbers(const std::string &input)
{
	std::string result;
	size_t i = 0;
	while (i < input.length()) {
		if (i + 2 < input.length() && input[i] == '0' && input[i + 1] == 'x') {
			result += "0x";
			i += 2;
			size_t hex_start = i;
			while (i < input.length() &&
			       ((input[i] >= '0' && input[i] <= '9') ||
				(input[i] >= 'a' && input[i] <= 'f') ||
				(input[i] >= 'A' && input[i] <= 'F'))) {
				i++;
			}
			size_t hex_len = i - hex_start;
			if (hex_len > 0) {
				std::string hex_digits = input.substr(hex_start, hex_len);
				size_t first_non_zero = 0;
				while (first_non_zero < hex_digits.length() && hex_digits[first_non_zero] == '0') {
					first_non_zero++;
				}
				if (first_non_zero == hex_digits.length()) {
					result += "0";
				} else {
					result += hex_digits.substr(first_non_zero);
				}
			}
		} else {
			result += input[i];
			i++;
		}
	}
	return result;
}

std::string replace_all_substrings(std::string str, std::string_view from, std::string_view to)
{
	size_t pos = 0;
	while ((pos = str.find(from, pos)) != std::string::npos) {
		str.replace(pos, from.size(), to);
		pos += to.size();
	}
	return str;
}

std::string shorten_symbol_for_disasm(std::string_view raw_name, const elf_hex_highlighter::parsed_symbol *enclosing_sym)
{
	auto simplify_one = [](std::string_view input) {
		std::string s(input);
		s = replace_all_substrings(std::move(s), "std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >", "std::string");
		s = replace_all_substrings(std::move(s), "std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char>>", "std::string");
		s = replace_all_substrings(std::move(s), "std::basic_string<char, std::char_traits<char>, std::allocator<char> >", "std::string");
		s = replace_all_substrings(std::move(s), "std::basic_string<char, std::char_traits<char>, std::allocator<char>>", "std::string");
		s = replace_all_substrings(std::move(s), "std::basic_string_view<char, std::char_traits<char> >", "std::string_view");
		s = replace_all_substrings(std::move(s), "std::basic_string_view<char, std::char_traits<char>>", "std::string_view");

		std::string out;
		out.reserve(s.size());
		int paren_depth = 0;
		for (size_t i = 0; i < s.size(); ++i) {
			if (s[i] == '(') {
				++paren_depth;
			} else if (s[i] == ')') {
				if (paren_depth > 0) {
					--paren_depth;
					if (paren_depth == 0 && std::string_view(s).substr(i + 1).starts_with(" const")) {
						i += 6;
					}
				}
			} else if (paren_depth == 0) {
				out.push_back(s[i]);
			}
		}
		return out;
	};

	std::string short_target = simplify_one(raw_name);
	if (enclosing_sym) {
		std::string short_enclosing = simplify_one(enclosing_sym->name);
		if (!short_enclosing.empty()) {
			short_target = replace_all_substrings(std::move(short_target), short_enclosing + "::", "");
		}
	}
	return short_target;
}

} // namespace

bool elf_hex_highlighter::can_handle(std::span<const uint8_t> data) const
{
	if (data.size() < 64)
		return false;
	return data[0] == 0x7F && data[1] == 'E' && data[2] == 'L' && data[3] == 'F';
}

bool elf_hex_highlighter::parse(std::span<const uint8_t> data)
{
	parsed_successfully_ = false;
	sections_.clear();
	symbols_.clear();
	object_symbols_.clear();
	got_plt_symbols_.clear();
	last_code_sec_idx_ = SIZE_MAX;
	last_code_inst_start_ = 0;

	if (!can_handle(data))
		return false;

	// Query MIME type and description using central helpers
	std::string_view view(reinterpret_cast<const char*>(data.data()), data.size());
	mime_type_ = mime::detect_buffer_type(view);
	description_ = mime::detect_buffer_description(view);

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
	if (header_.e_shoff > 0 && header_.e_shnum > 0 && header_.e_shentsize >= (header_.is_64 ? 64 : 40) && header_.e_shoff <= data.size()) {
		size_t max_entries = (data.size() - header_.e_shoff) / header_.e_shentsize;
		// First find string table offset
		uint64_t strtab_offset = 0;
		uint64_t strtab_size = 0;
		if (header_.e_shstrndx < max_entries) {
			size_t strtab_entry_start = header_.e_shoff + header_.e_shstrndx * header_.e_shentsize;
			if (header_.is_64) {
				strtab_offset = read_u64(data, strtab_entry_start + 24, lsb);
				strtab_size = read_u64(data, strtab_entry_start + 32, lsb);
			} else {
				strtab_offset = read_u32(data, strtab_entry_start + 16, lsb);
				strtab_size = read_u32(data, strtab_entry_start + 20, lsb);
			}
		}

		size_t valid_shnum = std::min(static_cast<size_t>(header_.e_shnum), max_entries);
		for (size_t i = 1; i < valid_shnum; ++i) {
			size_t entry_start = header_.e_shoff + i * header_.e_shentsize;
			if (entry_start > data.size() || header_.e_shentsize > data.size() - entry_start)
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
				sec.addr = read_u64(data, entry_start + 16, lsb);
				sec.offset = read_u64(data, entry_start + 24, lsb);
				sec.size = read_u64(data, entry_start + 32, lsb);
			} else {
				sec.type_val = read_u32(data, entry_start + 4, lsb);
				sec.addr = read_u32(data, entry_start + 12, lsb);
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

	// Parse symbol tables (.symtab / .dynsym) and PLT relocations (.rela.plt / .rela.dyn) bounds-safely if SHT exists
	if (header_.e_shoff > 0 && header_.e_shnum > 0 && header_.e_shentsize >= (header_.is_64 ? 64 : 40) && header_.e_shoff <= data.size()) {
		size_t max_entries = (data.size() - header_.e_shoff) / header_.e_shentsize;
		size_t valid_shnum = std::min(static_cast<size_t>(header_.e_shnum), max_entries);
		uint16_t e_type = read_u16(data, 16, lsb);

		for (size_t i = 1; i < valid_shnum; ++i) {
			size_t entry_start = header_.e_shoff + i * header_.e_shentsize;
			if (entry_start > data.size() || header_.e_shentsize > data.size() - entry_start)
				break;
			uint32_t type = read_u32(data, entry_start + 4, lsb);
			if (type == 2 || type == 11) { // SHT_SYMTAB or SHT_DYNSYM
				uint64_t symtab_offset = 0;
				uint64_t symtab_size = 0;
				uint64_t symtab_entsize = 0;
				uint32_t symtab_link = 0;
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

				uint64_t symstr_offset = 0;
				uint64_t symstr_size = 0;
				if (symtab_offset > 0 && symtab_size > 0 && symtab_entsize > 0 && symtab_link < valid_shnum) {
					size_t str_entry = header_.e_shoff + symtab_link * header_.e_shentsize;
					if (str_entry <= data.size() && header_.e_shentsize <= data.size() - str_entry) {
						if (header_.is_64) {
							symstr_offset = read_u64(data, str_entry + 24, lsb);
							symstr_size = read_u64(data, str_entry + 32, lsb);
						} else {
							symstr_offset = read_u32(data, str_entry + 16, lsb);
							symstr_size = read_u32(data, str_entry + 20, lsb);
						}
					}

					size_t req_sym_size = header_.is_64 ? 24 : 16;
					if (symtab_entsize >= req_sym_size && symtab_offset <= data.size()) {
						size_t max_syms = (data.size() - symtab_offset) / symtab_entsize;
						size_t sym_count = std::min(static_cast<size_t>(symtab_size / symtab_entsize), max_syms);
						for (size_t j = 0; j < sym_count; ++j) {
							size_t sym_start = symtab_offset + j * symtab_entsize;
							if (sym_start > data.size() || req_sym_size > data.size() - sym_start)
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
							if ((sym_type == 1 || sym_type == 2) && st_size > 0 && st_shndx > 0 && st_shndx < header_.e_shnum) {
								uint64_t sym_file_offset = 0;
								bool resolved = false;

								for (const auto &sec : sections_) {
									if (sec.index == st_shndx) {
										if (e_type == 1) { // ET_REL
											sym_file_offset = sec.offset + st_value;
										} else if (st_value >= sec.addr && st_value < sec.addr + sec.size) {
											sym_file_offset = sec.offset + (st_value - sec.addr);
										} else {
											continue;
										}
										resolved = true;
										break;
									}
								}

								parsed_symbol sym;
								if (symstr_offset > 0 && st_name < symstr_size) {
									sym.name = read_string(data, symstr_offset + st_name, 4096);
									sym.name = demangle_section_name(sym.name);
								} else if (sym_type == 2) {
									sym.name = std::format("sym_func_{:X}", st_value);
								}
								if (sym.name.empty()) {
									continue;
								}
								sym.offset = sym_file_offset;
								sym.size = st_size;
								sym.vaddr = st_value;

								if (sym_type == 2 && resolved) {
									bool dup = std::ranges::any_of(symbols_, [&](const parsed_symbol &s) {
										return s.offset == sym.offset;
									});
									if (!dup) {
										symbols_.push_back(std::move(sym));
									}
								} else if (sym_type == 1 && st_value > 0) {
									object_symbols_.push_back(std::move(sym));
								}
							}
						}
					}
				}
			} else if (type == 4 && header_.is_64) { // SHT_RELA (e.g. .rela.plt / .rela.dyn)
				uint64_t rela_offset = read_u64(data, entry_start + 24, lsb);
				uint64_t rela_size = read_u64(data, entry_start + 32, lsb);
				uint32_t rela_link = read_u32(data, entry_start + 40, lsb);
				uint64_t rela_entsize = read_u64(data, entry_start + 56, lsb);
				if (rela_offset > 0 && rela_size > 0 && rela_entsize >= 24 && rela_offset <= data.size() &&
				    rela_link > 0 && rela_link < valid_shnum) {
					size_t dynsym_hdr = header_.e_shoff + rela_link * header_.e_shentsize;
					uint64_t dynsym_off = read_u64(data, dynsym_hdr + 24, lsb);
					uint64_t dynsym_sz = read_u64(data, dynsym_hdr + 32, lsb);
					uint32_t dynstr_link = read_u32(data, dynsym_hdr + 40, lsb);
					uint64_t dynsym_entsz = read_u64(data, dynsym_hdr + 56, lsb);
					if (dynsym_off > 0 && dynsym_entsz >= 24 && dynsym_off <= data.size() &&
					    dynstr_link > 0 && dynstr_link < valid_shnum) {
						size_t dynstr_hdr = header_.e_shoff + dynstr_link * header_.e_shentsize;
						uint64_t dynstr_off = read_u64(data, dynstr_hdr + 24, lsb);
						uint64_t dynstr_sz = read_u64(data, dynstr_hdr + 32, lsb);
						size_t max_relas = (data.size() - rela_offset) / rela_entsize;
						size_t rela_count = std::min(static_cast<size_t>(rela_size / rela_entsize), max_relas);
						size_t max_dynsyms = (data.size() - dynsym_off) / dynsym_entsz;
						size_t dynsym_count = std::min(static_cast<size_t>(dynsym_sz / dynsym_entsz), max_dynsyms);
						for (size_t r = 0; r < rela_count; ++r) {
							size_t r_start = rela_offset + r * rela_entsize;
							uint64_t r_offset = read_u64(data, r_start, lsb);
							uint64_t r_info = read_u64(data, r_start + 8, lsb);
							uint32_t sym_idx = static_cast<uint32_t>(r_info >> 32);
							if (r_offset > 0 && sym_idx > 0 && sym_idx < dynsym_count) {
								size_t d_start = dynsym_off + sym_idx * dynsym_entsz;
								uint32_t st_name = read_u32(data, d_start, lsb);
								if (dynstr_off > 0 && st_name < dynstr_sz) {
									std::string sname = read_string(data, dynstr_off + st_name, 4096);
									if (!sname.empty()) {
										sname = demangle_section_name(sname) + "@plt";
										got_plt_symbols_.push_back({std::move(sname), 0, 8, r_offset});
									}
								}
							}
						}
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

highlight_info elf_hex_highlighter::get_info(std::span<const uint8_t> data, size_t offset) const
{
	if (!parsed_successfully_ || offset >= data.size())
		return {};

	auto clamp_and_return = [&](hex_semantic_type type, std::string desc, size_t start, size_t size) -> highlight_info {
		size_t clamped_size = (start < data.size()) ? std::min(size, data.size() - start) : 0;
		return {type, std::move(desc), start, clamped_size};
	};

	bool lsb = header_.is_lsb;

	// 1. Magic
	if (offset < 4) {
		return clamp_and_return(hex_semantic_type::magic, "ELF Magic Sign: '\\x7fELF'", 0, 4);
	}

	// 2. Identification details
	if (offset >= 4 && offset < 16) {
		if (offset == 4) {
			return clamp_and_return(hex_semantic_type::file_header, std::format("ELF Ehdr: e_ident[EI_CLASS] = {}", get_class_desc(data[4])), 4, 1);
		}
		if (offset == 5) {
			return clamp_and_return(hex_semantic_type::file_header, std::format("ELF Ehdr: e_ident[EI_DATA] = {}", get_data_desc(data[5])), 5, 1);
		}
		if (offset == 6) {
			return clamp_and_return(hex_semantic_type::file_header, std::format("ELF Ehdr: e_ident[EI_VERSION] = {}", data[6]), 6, 1);
		}
		if (offset == 7) {
			return clamp_and_return(hex_semantic_type::file_header, std::format("ELF Ehdr: e_ident[EI_OSABI] = {}", get_osabi_desc(data[7])), 7, 1);
		}
		if (offset == 8) {
			return clamp_and_return(hex_semantic_type::file_header, std::format("ELF Ehdr: e_ident[EI_ABIVERSION] = {}", data[8]), 8, 1);
		}
		return clamp_and_return(hex_semantic_type::file_header, std::format("ELF Ehdr: e_ident padding [{}]", offset - 9), 9, 7);
	}

	// 3. Ehdr remaining fields
	if (offset < header_.e_ehsize) {
		size_t relative = offset;
		if (header_.is_64) {
			if (relative >= 16 && relative < 18) {
				uint16_t type = read_u16(data, 16, lsb);
				return clamp_and_return(hex_semantic_type::file_header, std::format("ELF Ehdr: e_type = {}", get_type_desc(type)), 16, 2);
			}
			if (relative >= 18 && relative < 20) {
				uint16_t mach = read_u16(data, 18, lsb);
				return clamp_and_return(hex_semantic_type::file_header, std::format("ELF Ehdr: e_machine = {}", get_machine_desc(mach)), 18, 2);
			}
			if (relative >= 20 && relative < 24) {
				uint32_t ver = read_u32(data, 20, lsb);
				return clamp_and_return(hex_semantic_type::file_header, std::format("ELF Ehdr: e_version = {}", ver), 20, 4);
			}
			if (relative >= 24 && relative < 32) {
				uint64_t ent = read_u64(data, 24, lsb);
				return clamp_and_return(hex_semantic_type::file_header, std::format("ELF Ehdr: e_entry = 0x{:08X}", ent), 24, 8);
			}
			if (relative >= 32 && relative < 40) {
				uint64_t phoff = read_u64(data, 32, lsb);
				return clamp_and_return(hex_semantic_type::file_header, std::format("ELF Ehdr: e_phoff = 0x{:X}", phoff), 32, 8);
			}
			if (relative >= 40 && relative < 48) {
				uint64_t shoff = read_u64(data, 40, lsb);
				return clamp_and_return(hex_semantic_type::file_header, std::format("ELF Ehdr: e_shoff = 0x{:X}", shoff), 40, 8);
			}
			if (relative >= 48 && relative < 52) {
				uint32_t flags = read_u32(data, 48, lsb);
				return clamp_and_return(hex_semantic_type::file_header, std::format("ELF Ehdr: e_flags = 0x{:08X}", flags), 48, 4);
			}
			if (relative >= 52 && relative < 54) {
				return clamp_and_return(hex_semantic_type::file_header, std::format("ELF Ehdr: e_ehsize = {} bytes", header_.e_ehsize), 52, 2);
			}
			if (relative >= 54 && relative < 56) {
				return clamp_and_return(hex_semantic_type::file_header,
					std::format("ELF Ehdr: e_phentsize = {} bytes", header_.e_phentsize), 54, 2);
			}
			if (relative >= 56 && relative < 58) {
				return clamp_and_return(hex_semantic_type::file_header, std::format("ELF Ehdr: e_phnum = {}", header_.e_phnum), 56, 2);
			}
			if (relative >= 58 && relative < 60) {
				return clamp_and_return(hex_semantic_type::file_header,
					std::format("ELF Ehdr: e_shentsize = {} bytes", header_.e_shentsize), 58, 2);
			}
			if (relative >= 60 && relative < 62) {
				return clamp_and_return(hex_semantic_type::file_header, std::format("ELF Ehdr: e_shnum = {}", header_.e_shnum), 60, 2);
			}
			if (relative >= 62 && relative < 64) {
				return clamp_and_return(hex_semantic_type::file_header, std::format("ELF Ehdr: e_shstrndx = {}", header_.e_shstrndx), 62, 2);
			}
		} else {
			// 32-bit Ehdr
			if (relative >= 16 && relative < 18) {
				uint16_t type = read_u16(data, 16, lsb);
				return clamp_and_return(hex_semantic_type::file_header, std::format("ELF Ehdr: e_type = {}", get_type_desc(type)), 16, 2);
			}
			if (relative >= 18 && relative < 20) {
				uint16_t mach = read_u16(data, 18, lsb);
				return clamp_and_return(hex_semantic_type::file_header, std::format("ELF Ehdr: e_machine = {}", get_machine_desc(mach)), 18, 2);
			}
			if (relative >= 20 && relative < 24) {
				uint32_t ver = read_u32(data, 20, lsb);
				return clamp_and_return(hex_semantic_type::file_header, std::format("ELF Ehdr: e_version = {}", ver), 20, 4);
			}
			if (relative >= 24 && relative < 28) {
				uint32_t ent = read_u32(data, 24, lsb);
				return clamp_and_return(hex_semantic_type::file_header, std::format("ELF Ehdr: e_entry = 0x{:08X}", ent), 24, 4);
			}
			if (relative >= 28 && relative < 32) {
				uint32_t phoff = read_u32(data, 28, lsb);
				return clamp_and_return(hex_semantic_type::file_header, std::format("ELF Ehdr: e_phoff = 0x{:X}", phoff), 28, 4);
			}
			if (relative >= 32 && relative < 36) {
				uint32_t shoff = read_u32(data, 32, lsb);
				return clamp_and_return(hex_semantic_type::file_header, std::format("ELF Ehdr: e_shoff = 0x{:X}", shoff), 32, 4);
			}
			if (relative >= 36 && relative < 40) {
				uint32_t flags = read_u32(data, 36, lsb);
				return clamp_and_return(hex_semantic_type::file_header, std::format("ELF Ehdr: e_flags = 0x{:08X}", flags), 36, 4);
			}
			if (relative >= 40 && relative < 42) {
				return clamp_and_return(hex_semantic_type::file_header, std::format("ELF Ehdr: e_ehsize = {} bytes", header_.e_ehsize), 40, 2);
			}
			if (relative >= 42 && relative < 44) {
				return clamp_and_return(hex_semantic_type::file_header,
					std::format("ELF Ehdr: e_phentsize = {} bytes", header_.e_phentsize), 42, 2);
			}
			if (relative >= 44 && relative < 46) {
				return clamp_and_return(hex_semantic_type::file_header, std::format("ELF Ehdr: e_phnum = {}", header_.e_phnum), 44, 2);
			}
			if (relative >= 46 && relative < 48) {
				return clamp_and_return(hex_semantic_type::file_header,
					std::format("ELF Ehdr: e_shentsize = {} bytes", header_.e_shentsize), 46, 2);
			}
			if (relative >= 48 && relative < 50) {
				return clamp_and_return(hex_semantic_type::file_header, std::format("ELF Ehdr: e_shnum = {}", header_.e_shnum), 48, 2);
			}
			if (relative >= 50 && relative < 52) {
				return clamp_and_return(hex_semantic_type::file_header, std::format("ELF Ehdr: e_shstrndx = {}", header_.e_shstrndx), 50, 2);
			}
		}
		return clamp_and_return(hex_semantic_type::file_header, "ELF Ehdr Header", 0, header_.e_ehsize);
	}

	// 4. Program Header Table
	if (header_.e_phoff > 0 && header_.e_phnum > 0 && header_.e_phentsize > 0 && header_.e_phoff <= data.size()) {
		size_t max_entries = (data.size() - header_.e_phoff) / header_.e_phentsize;
		size_t valid_entries = std::min(static_cast<size_t>(header_.e_phnum), max_entries);
		size_t ph_table_end = header_.e_phoff + (valid_entries * header_.e_phentsize);
		if (offset >= header_.e_phoff && offset < ph_table_end) {
			size_t entry_idx = (offset - header_.e_phoff) / header_.e_phentsize;
			size_t field_offset = (offset - header_.e_phoff) % header_.e_phentsize;
			size_t entry_start = header_.e_phoff + entry_idx * header_.e_phentsize;

			if (header_.is_64) {
				if (field_offset < 4) {
					uint32_t type = read_u32(data, entry_start, lsb);
					return clamp_and_return(hex_semantic_type::prog_header,
						std::format("ELF Phdr[{}]: p_type = {}", entry_idx, get_phdr_type_desc(type)),
						entry_start, 4);
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
					return clamp_and_return(hex_semantic_type::prog_header,
						std::format("ELF Phdr[{}]: p_flags = 0x{:X} ({})", entry_idx, flags, f_str),
						entry_start + 4, 4);
				}
				if (field_offset >= 8 && field_offset < 16) {
					uint64_t val = read_u64(data, entry_start + 8, lsb);
					return clamp_and_return(hex_semantic_type::prog_header,
						std::format("ELF Phdr[{}]: p_offset = 0x{:X}", entry_idx, val),
						entry_start + 8, 8);
				}
				if (field_offset >= 16 && field_offset < 24) {
					uint64_t val = read_u64(data, entry_start + 16, lsb);
					return clamp_and_return(hex_semantic_type::prog_header,
						std::format("ELF Phdr[{}]: p_vaddr = 0x{:X}", entry_idx, val),
						entry_start + 16, 8);
				}
				if (field_offset >= 24 && field_offset < 32) {
					uint64_t val = read_u64(data, entry_start + 24, lsb);
					return clamp_and_return(hex_semantic_type::prog_header,
						std::format("ELF Phdr[{}]: p_paddr = 0x{:X}", entry_idx, val),
						entry_start + 24, 8);
				}
				if (field_offset >= 32 && field_offset < 40) {
					uint64_t val = read_u64(data, entry_start + 32, lsb);
					return clamp_and_return(hex_semantic_type::prog_header,
						std::format("ELF Phdr[{}]: p_filesz = {} bytes (0x{:X})", entry_idx, val, val),
						entry_start + 32, 8);
				}
				if (field_offset >= 40 && field_offset < 48) {
					uint64_t val = read_u64(data, entry_start + 40, lsb);
					return clamp_and_return(hex_semantic_type::prog_header,
						std::format("ELF Phdr[{}]: p_memsz = {} bytes (0x{:X})", entry_idx, val, val),
						entry_start + 40, 8);
				}
				if (field_offset >= 48 && field_offset < 56) {
					uint64_t val = read_u64(data, entry_start + 48, lsb);
					return clamp_and_return(hex_semantic_type::prog_header,
						std::format("ELF Phdr[{}]: p_align = 0x{:X}", entry_idx, val),
						entry_start + 48, 8);
				}
			} else {
				// 32-bit Phdr
				if (field_offset < 4) {
					uint32_t type = read_u32(data, entry_start, lsb);
					return clamp_and_return(hex_semantic_type::prog_header,
						std::format("ELF Phdr[{}]: p_type = {}", entry_idx, get_phdr_type_desc(type)),
						entry_start, 4);
				}
				if (field_offset >= 4 && field_offset < 8) {
					uint32_t val = read_u32(data, entry_start + 4, lsb);
					return clamp_and_return(hex_semantic_type::prog_header,
						std::format("ELF Phdr[{}]: p_offset = 0x{:X}", entry_idx, val),
						entry_start + 4, 4);
				}
				if (field_offset >= 8 && field_offset < 12) {
					uint32_t val = read_u32(data, entry_start + 8, lsb);
					return clamp_and_return(hex_semantic_type::prog_header,
						std::format("ELF Phdr[{}]: p_vaddr = 0x{:X}", entry_idx, val),
						entry_start + 8, 4);
				}
				if (field_offset >= 12 && field_offset < 16) {
					uint32_t val = read_u32(data, entry_start + 12, lsb);
					return clamp_and_return(hex_semantic_type::prog_header,
						std::format("ELF Phdr[{}]: p_paddr = 0x{:X}", entry_idx, val),
						entry_start + 12, 4);
				}
				if (field_offset >= 16 && field_offset < 20) {
					uint32_t val = read_u32(data, entry_start + 16, lsb);
					return clamp_and_return(hex_semantic_type::prog_header,
						std::format("ELF Phdr[{}]: p_filesz = {} bytes (0x{:X})", entry_idx, val, val),
						entry_start + 16, 4);
				}
				if (field_offset >= 20 && field_offset < 24) {
					uint32_t val = read_u32(data, entry_start + 20, lsb);
					return clamp_and_return(hex_semantic_type::prog_header,
						std::format("ELF Phdr[{}]: p_memsz = {} bytes (0x{:X})", entry_idx, val, val),
						entry_start + 20, 4);
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
					return clamp_and_return(hex_semantic_type::prog_header,
						std::format("ELF Phdr[{}]: p_flags = 0x{:X} ({})", entry_idx, flags, f_str),
						entry_start + 24, 4);
				}
				if (field_offset >= 28 && field_offset < 32) {
					uint32_t val = read_u32(data, entry_start + 28, lsb);
					return clamp_and_return(hex_semantic_type::prog_header,
						std::format("ELF Phdr[{}]: p_align = 0x{:X}", entry_idx, val),
						entry_start + 28, 4);
				}
			}
			return clamp_and_return(hex_semantic_type::prog_header, std::format("ELF Phdr[{}] Segment entry", entry_idx),
				entry_start, header_.e_phentsize);
		}
	}

	// 5. Section Header Table
	if (header_.e_shoff > 0 && header_.e_shnum > 0 && header_.e_shentsize > 0 && header_.e_shoff <= data.size()) {
		size_t max_entries = (data.size() - header_.e_shoff) / header_.e_shentsize;
		size_t valid_entries = std::min(static_cast<size_t>(header_.e_shnum), max_entries);
		size_t sh_table_end = header_.e_shoff + (valid_entries * header_.e_shentsize);
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
					return clamp_and_return(
					    hex_semantic_type::sect_header,
					    std::format("ELF Shdr[{}]{}: sh_name = 0x{:X} String offset", entry_idx, sec_name_label, val),
					    entry_start, 4);
				}
				if (field_offset >= 4 && field_offset < 8) {
					uint32_t type = read_u32(data, entry_start + 4, lsb);
					return clamp_and_return(hex_semantic_type::sect_header, std::format("ELF Shdr[{}]{}: sh_type = {}", entry_idx,
											    sec_name_label, get_shdr_type_desc(type)),
						entry_start + 4, 4);
				}
				if (field_offset >= 8 && field_offset < 16) {
					uint64_t val = read_u64(data, entry_start + 8, lsb);
					return clamp_and_return(hex_semantic_type::sect_header, std::format("ELF Shdr[{}]{}: sh_flags = {}", entry_idx,
											    sec_name_label, get_shdr_flags_desc(val)),
						entry_start + 8, 8);
				}
				if (field_offset >= 16 && field_offset < 24) {
					uint64_t val = read_u64(data, entry_start + 16, lsb);
					return clamp_and_return(hex_semantic_type::sect_header,
						std::format("ELF Shdr[{}]{}: sh_addr = 0x{:08X}", entry_idx, sec_name_label, val),
						entry_start + 16, 8);
				}
				if (field_offset >= 24 && field_offset < 32) {
					uint64_t val = read_u64(data, entry_start + 24, lsb);
					return clamp_and_return(hex_semantic_type::sect_header,
						std::format("ELF Shdr[{}]{}: sh_offset = 0x{:X}", entry_idx, sec_name_label, val),
						entry_start + 24, 8);
				}
				if (field_offset >= 32 && field_offset < 40) {
					uint64_t val = read_u64(data, entry_start + 32, lsb);
					return clamp_and_return(hex_semantic_type::sect_header, std::format("ELF Shdr[{}]{}: sh_size = {} bytes (0x{:X})",
											    entry_idx, sec_name_label, val, val),
						entry_start + 32, 8);
				}
				if (field_offset >= 40 && field_offset < 44) {
					uint32_t val = read_u32(data, entry_start + 40, lsb);
					return clamp_and_return(hex_semantic_type::sect_header,
						std::format("ELF Shdr[{}]{}: sh_link = {}", entry_idx, sec_name_label, val),
						entry_start + 40, 4);
				}
				if (field_offset >= 44 && field_offset < 48) {
					uint32_t val = read_u32(data, entry_start + 44, lsb);
					return clamp_and_return(hex_semantic_type::sect_header,
						std::format("ELF Shdr[{}]{}: sh_info = {}", entry_idx, sec_name_label, val),
						entry_start + 44, 4);
				}
				if (field_offset >= 48 && field_offset < 56) {
					uint64_t val = read_u64(data, entry_start + 48, lsb);
					return clamp_and_return(hex_semantic_type::sect_header,
						std::format("ELF Shdr[{}]{}: sh_addralign = {}", entry_idx, sec_name_label, val),
						entry_start + 48, 8);
				}
				if (field_offset >= 56 && field_offset < 64) {
					uint64_t val = read_u64(data, entry_start + 56, lsb);
					return clamp_and_return(hex_semantic_type::sect_header,
						std::format("ELF Shdr[{}]{}: sh_entsize = {} bytes", entry_idx, sec_name_label, val),
						entry_start + 56, 8);
				}
			} else {
				// 32-bit Shdr
				if (field_offset < 4) {
					uint32_t val = read_u32(data, entry_start, lsb);
					return clamp_and_return(
					    hex_semantic_type::sect_header,
					    std::format("ELF Shdr[{}]{}: sh_name = 0x{:X} String offset", entry_idx, sec_name_label, val),
					    entry_start, 4);
				}
				if (field_offset >= 4 && field_offset < 8) {
					uint32_t type = read_u32(data, entry_start + 4, lsb);
					return clamp_and_return(hex_semantic_type::sect_header, std::format("ELF Shdr[{}]{}: sh_type = {}", entry_idx,
											    sec_name_label, get_shdr_type_desc(type)),
						entry_start + 4, 4);
				}
				if (field_offset >= 8 && field_offset < 12) {
					uint32_t val = read_u32(data, entry_start + 8, lsb);
					return clamp_and_return(hex_semantic_type::sect_header, std::format("ELF Shdr[{}]{}: sh_flags = {}", entry_idx,
											    sec_name_label, get_shdr_flags_desc(val)),
						entry_start + 8, 4);
				}
				if (field_offset >= 12 && field_offset < 16) {
					uint32_t val = read_u32(data, entry_start + 12, lsb);
					return clamp_and_return(hex_semantic_type::sect_header,
						std::format("ELF Shdr[{}]{}: sh_addr = 0x{:08X}", entry_idx, sec_name_label, val),
						entry_start + 12, 4);
				}
				if (field_offset >= 16 && field_offset < 20) {
					uint32_t val = read_u32(data, entry_start + 16, lsb);
					return clamp_and_return(hex_semantic_type::sect_header,
						std::format("ELF Shdr[{}]{}: sh_offset = 0x{:X}", entry_idx, sec_name_label, val),
						entry_start + 16, 4);
				}
				if (field_offset >= 20 && field_offset < 24) {
					uint32_t val = read_u32(data, entry_start + 20, lsb);
					return clamp_and_return(hex_semantic_type::sect_header, std::format("ELF Shdr[{}]{}: sh_size = {} bytes (0x{:X})",
											    entry_idx, sec_name_label, val, val),
						entry_start + 20, 4);
				}
				if (field_offset >= 24 && field_offset < 28) {
					uint32_t val = read_u32(data, entry_start + 24, lsb);
					return clamp_and_return(hex_semantic_type::sect_header,
						std::format("ELF Shdr[{}]{}: sh_link = {}", entry_idx, sec_name_label, val),
						entry_start + 24, 4);
				}
				if (field_offset >= 28 && field_offset < 32) {
					uint32_t val = read_u32(data, entry_start + 28, lsb);
					return clamp_and_return(hex_semantic_type::sect_header,
						std::format("ELF Shdr[{}]{}: sh_info = {}", entry_idx, sec_name_label, val),
						entry_start + 28, 4);
				}
				if (field_offset >= 32 && field_offset < 36) {
					uint32_t val = read_u32(data, entry_start + 32, lsb);
					return clamp_and_return(hex_semantic_type::sect_header,
						std::format("ELF Shdr[{}]{}: sh_addralign = {}", entry_idx, sec_name_label, val),
						entry_start + 32, 4);
				}
				if (field_offset >= 36 && field_offset < 40) {
					uint32_t val = read_u32(data, entry_start + 36, lsb);
					return clamp_and_return(hex_semantic_type::sect_header,
						std::format("ELF Shdr[{}]{}: sh_entsize = {} bytes", entry_idx, sec_name_label, val),
						entry_start + 36, 4);
				}
			}
			return clamp_and_return(hex_semantic_type::sect_header, std::format("ELF Shdr[{}] Section entry", entry_idx),
				entry_start, header_.e_shentsize);
		}
	}

	// 6. Section Ranges (linear scan for matches)
	for (const auto &sec : sections_) {
		if (offset >= sec.offset && offset < sec.offset + sec.size) {
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

				if (last_code_sec_idx_ == sec.index && last_code_inst_start_ >= start_offset &&
				    offset >= last_code_inst_start_ && offset - last_code_inst_start_ <= 64) {
					start_offset = last_code_inst_start_;
				} else if (!found_sym && offset - start_offset > 65536) {
					start_offset = offset;
				}

				if (limit_offset > data.size()) {
					limit_offset = data.size();
				}

				size_t inst_start = start_offset;
				ZyanU64 runtime_address = sec.addr + (inst_start - sec.offset);

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
						last_code_sec_idx_ = sec.index;
						last_code_inst_start_ = inst_start;
						std::string target_annotation;
						for (uint8_t op_idx = 0; op_idx < instruction.info.operand_count_visible; ++op_idx) {
							const auto &op = instruction.operands[op_idx];
							bool is_rel_imm = (op.type == ZYDIS_OPERAND_TYPE_IMMEDIATE && op.imm.is_relative);
							bool is_rip_mem = (op.type == ZYDIS_OPERAND_TYPE_MEMORY &&
							                   (op.mem.base == ZYDIS_REGISTER_RIP || op.mem.base == ZYDIS_REGISTER_EIP));
							if (!is_rel_imm && !is_rip_mem) {
								continue;
							}
							int64_t rel_disp = is_rel_imm ? op.imm.value.s : op.mem.disp.value;
							uint64_t target_vaddr = static_cast<uint64_t>(static_cast<int64_t>(runtime_address + inst_len) + rel_disp);
							if (!header_.is_64) {
								target_vaddr &= 0xFFFFFFFFULL;
							}
							target_annotation = resolve_target_annotation(data, target_vaddr, is_rip_mem, found_sym);
							if (!target_annotation.empty()) {
								break;
							}
						}
						std::string symbol_ctx;
						if (found_sym) {
							symbol_ctx = std::format(" (in {} + 0x{:X})", found_sym->name, inst_start - found_sym->offset);
						}
						return clamp_and_return(sec.semantic,
							std::format("{}{}{} | ELF Sec \"{}\": type = {}, offset = 0x{:X}",
										  shorten_hex_numbers(instruction.text), target_annotation, symbol_ctx, sec.name,
										  get_shdr_type_desc(sec.type_val), sec.offset),
							inst_start, inst_len);
					}

					inst_start += inst_len;
					runtime_address += inst_len;
				}
				last_code_sec_idx_ = SIZE_MAX;
			}
#endif
			return clamp_and_return(sec.semantic,
				std::format("ELF Sec \"{}\": type = {}, offset = 0x{:X}", sec.name,
							  get_shdr_type_desc(sec.type_val), sec.offset),
				sec.offset, sec.size);
		}
	}

	return {hex_semantic_type::normal, "", 0, 0};
}

std::string elf_hex_highlighter::shorten_hex(std::string_view input)
{
	return shorten_hex_numbers(std::string(input));
}

const elf_hex_highlighter::parsed_symbol *elf_hex_highlighter::find_symbol_by_name(std::string_view name) const
{
	if (!parsed_successfully_ || name.empty()) {
		return nullptr;
	}
	for (const auto &sym : symbols_) {
		if (sym.name == name) {
			return &sym;
		}
	}
	for (const auto &sym : symbols_) {
		size_t paren = sym.name.find('(');
		if (paren != std::string::npos && std::string_view(sym.name).substr(0, paren) == name) {
			return &sym;
		}
	}
	for (const auto &sym : symbols_) {
		size_t paren = sym.name.find('(');
		std::string_view base = (paren != std::string::npos) ? std::string_view(sym.name).substr(0, paren) : std::string_view(sym.name);
		if (base.size() > name.size() + 2 && base.ends_with(name) &&
		    base.substr(base.size() - name.size() - 2, 2) == "::") {
			return &sym;
		}
	}
	const parsed_symbol *substr_match = nullptr;
	size_t match_count = 0;
	for (const auto &sym : symbols_) {
		if (sym.name.find(name) != std::string::npos) {
			substr_match = &sym;
			++match_count;
		}
	}
	if (match_count == 1) {
		return substr_match;
	}
	return nullptr;
}

const elf_hex_highlighter::parsed_symbol *elf_hex_highlighter::find_symbol_at_offset(size_t offset) const
{
	if (!parsed_successfully_) {
		return nullptr;
	}
	for (const auto &sym : symbols_) {
		if (offset >= sym.offset && offset < sym.offset + sym.size) {
			return &sym;
		}
	}
	return nullptr;
}

const elf_hex_highlighter::parsed_section *elf_hex_highlighter::find_section_by_name(std::string_view name) const
{
	if (!parsed_successfully_ || name.empty()) {
		return nullptr;
	}
	for (const auto &sec : sections_) {
		if (sec.name == name) {
			return &sec;
		}
	}
	return nullptr;
}

const elf_hex_highlighter::parsed_section *elf_hex_highlighter::find_section_at_offset(size_t offset) const
{
	if (!parsed_successfully_) {
		return nullptr;
	}
	for (const auto &sec : sections_) {
		if (offset >= sec.offset && offset < sec.offset + sec.size) {
			return &sec;
		}
	}
	return nullptr;
}

std::string elf_hex_highlighter::resolve_target_annotation(std::span<const uint8_t> data, uint64_t target_vaddr,
                                                           bool is_rip_mem, const parsed_symbol *found_sym,
                                                           bool include_self_offset) const
{
	const parsed_section *target_sec = nullptr;
	uint64_t target_offset = 0;
	for (const auto &sec : sections_) {
		if (sec.addr > 0 && target_vaddr >= sec.addr && target_vaddr < sec.addr + sec.size) {
			target_sec = &sec;
			target_offset = sec.offset + (target_vaddr - sec.addr);
			break;
		}
	}

	if (target_sec) {
		for (const auto &sym : symbols_) {
			if (target_offset >= sym.offset && target_offset < sym.offset + sym.size) {
				if (&sym == found_sym) {
					if (include_self_offset) {
						return std::format(" <+0x{:X}>", target_offset - sym.offset);
					}
					return "";
				}
				std::string short_name = shorten_symbol_for_disasm(sym.name, found_sym);
				if (target_offset > sym.offset) {
					return std::format(" <{}+0x{:X}>", short_name, target_offset - sym.offset);
				}
				return std::format(" <{}>", short_name);
			}
		}

		if (target_sec->name.starts_with(".plt") && target_offset <= data.size()) {
			size_t jmp_off = SIZE_MAX;
			if (6 <= data.size() - target_offset && data[target_offset] == 0xFF && data[target_offset + 1] == 0x25) {
				jmp_off = 0;
			} else if (10 <= data.size() - target_offset && data[target_offset] == 0xF3 && data[target_offset + 1] == 0x0F &&
			           data[target_offset + 2] == 0x1E && data[target_offset + 3] == 0xFA &&
			           data[target_offset + 4] == 0xFF && data[target_offset + 5] == 0x25) {
				jmp_off = 4;
			}
			if (jmp_off != SIZE_MAX) {
				int32_t disp = static_cast<int32_t>(read_u32(data, target_offset + jmp_off + 2, header_.is_lsb));
				uint64_t got_vaddr = static_cast<uint64_t>(static_cast<int64_t>(target_vaddr + jmp_off + 6) + disp);
				for (const auto &gsym : got_plt_symbols_) {
					if (gsym.vaddr == got_vaddr) {
						return std::format(" <{}>", shorten_symbol_for_disasm(gsym.name, found_sym));
					}
				}
			}
		}
	}

	for (const auto &osym : object_symbols_) {
		if (target_vaddr >= osym.vaddr && target_vaddr < osym.vaddr + osym.size) {
			std::string short_name = shorten_symbol_for_disasm(osym.name, found_sym);
			if (target_vaddr > osym.vaddr) {
				return std::format(" <{}+0x{:X}>", short_name, target_vaddr - osym.vaddr);
			}
			return std::format(" <{}>", short_name);
		}
	}

	for (const auto &gsym : got_plt_symbols_) {
		if (gsym.vaddr == target_vaddr) {
			return std::format(" <{}>", shorten_symbol_for_disasm(gsym.name, found_sym));
		}
	}

	if (is_rip_mem && target_sec &&
	    (target_sec->name.starts_with(".rodata") || target_sec->semantic == hex_semantic_type::rodata_section) &&
	    target_offset < data.size()) {
		size_t max_scan = std::min<size_t>(257, data.size() - target_offset);
		std::string literal;
		bool valid_str = false;
		bool truncated = false;
		for (size_t k = 0; k < max_scan; ++k) {
			uint8_t ch = data[target_offset + k];
			if (ch == 0) {
				valid_str = (k >= 2);
				break;
			}
			if ((ch < 32 || ch > 126) && ch != '\n' && ch != '\r' && ch != '\t') {
				valid_str = false;
				break;
			}
			if (k == 256) {
				valid_str = true;
				truncated = true;
				break;
			}
			if (ch == '\n') {
				literal += "\\n";
			} else if (ch == '\r') {
				literal += "\\r";
			} else if (ch == '\t') {
				literal += "\\t";
			} else {
				if (ch == '"' || ch == '\\') {
					literal.push_back('\\');
				}
				literal.push_back(static_cast<char>(ch));
			}
		}
		if (valid_str) {
			return std::format(" <\"{}{}\">", literal, truncated ? "..." : "");
		}
	}

	return "";
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

std::optional<size_t> elf_hex_highlighter::get_offset_by_name(std::string_view name) const
{
	if (!parsed_successfully_) {
		return std::nullopt;
	}
	if (const auto *sec = find_section_by_name(name)) {
		return sec->offset;
	}
	if (const auto *sym = find_symbol_by_name(name)) {
		return sym->offset;
	}
	for (const auto &osym : object_symbols_) {
		if (osym.offset > 0 && osym.name == name) {
			return osym.offset;
		}
	}
	return std::nullopt;
}

std::optional<size_t> elf_hex_highlighter::get_size_by_name(std::string_view name) const
{
	if (!parsed_successfully_) {
		return std::nullopt;
	}
	if (const auto *sec = find_section_by_name(name)) {
		if (sec->size > 0) {
			return sec->size;
		}
	}
	if (const auto *sym = find_symbol_by_name(name)) {
		if (sym->size > 0) {
			return sym->size;
		}
	}
	for (const auto &osym : object_symbols_) {
		if (osym.offset > 0 && osym.size > 0 && osym.name == name) {
			return osym.size;
		}
	}
	return std::nullopt;
}
std::string elf_hex_highlighter::get_structure_summary() const
{
	if (!parsed_successfully_) {
		return "";
	}

	std::string summary = "### ELF Structure Overview\n\n";

	// Add Metadata Table
	summary += "#### File Metadata\n\n";
	summary += "| Property | Value |\n";
	summary += "| --- | --- |\n";
	summary += std::format("| **MIME Type** | {} |\n", mime_type_);
	summary += std::format("| **Description** | {} |\n\n", description_);

	summary += "#### ELF Header Properties\n\n";
	summary += std::format("- **Class**: {}\n", header_.is_64 ? "ELF64" : "ELF32");
	summary += std::format("- **Endianness**: {}\n", header_.is_lsb ? "LSB (Little Endian)" : "MSB (Big Endian)");
	summary += std::format("- **Entry Point**: 0x{:X}\n", header_.e_entry);
	summary += std::format("- **Section Header Table Offset**: 0x{:X} ({} entries)\n", header_.e_shoff, header_.e_shnum);
	summary += std::format("- **Program Header Table Offset**: 0x{:X} ({} entries)\n\n", header_.e_phoff, header_.e_phnum);

	if (!sections_.empty()) {
		summary += "#### ELF Sections\n\n";
		summary += "| Index | Name | Type | Offset | Size (Bytes) |\n";
		summary += "| :---: | :--- | :---: | :---: | :---: |\n";
		for (const auto &sec : sections_) {
			summary += std::format("| `{}` | `{}` | `0x{:X}` | `0x{:X}` | `{}` |\n",
			                       sec.index, sec.name, sec.type_val, sec.offset, sec.size);
		}
		summary += "\n";
	}

	if (!symbols_.empty()) {
		summary += "#### ELF Symbols\n\n";
		summary += "| Name | Offset | Size (Bytes) |\n";
		summary += "| :--- | :---: | :---: |\n";
		for (const auto &sym : symbols_) {
			summary += std::format("| `{}` | `0x{:X}` | `{}` |\n",
			                       sym.name, sym.offset, sym.size);
		}
		summary += "\n";
	}

	return summary;
}
