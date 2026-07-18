#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <optional>

enum class hex_semantic_type {
	normal,
	magic,
	file_header,
	prog_header,
	sect_header,
	code_section,
	data_section,
	rodata_section,
	symtab_section
};

struct highlight_info {
	hex_semantic_type type{hex_semantic_type::normal};
	std::string description;
	size_t range_start{0};
	size_t range_size{0};
};

/*

# subclasses of hex_highlighter

| subclass            | filename        |
| ------------------- | --------------- | 
| elf_hex_highlighter | src/hex/elf.h   |
| png_hex_highlighter | src/hex/png.h   |
| jpeg_hex_highlighter| src/hex/jpeg.h  |

*/

class hex_highlighter
{
      public:
	virtual ~hex_highlighter() = default;

	// Determine if this highlighter should be used for this data
	virtual bool can_handle(const std::vector<uint8_t> &data) const = 0;

	// Parse the data and cache structure offsets
	virtual bool parse(const std::vector<uint8_t> &data) = 0;

	// Query information for a byte offset
	virtual highlight_info get_info(const std::vector<uint8_t> &data, size_t offset) const = 0;

	// Query the next symbol boundary offset after the current_offset
	virtual size_t get_next_symbol_offset(size_t current_offset) const { return current_offset; }

	// Query offset by name (e.g. section name like ".text", symbol name, or chunk name like "PLTE")
	virtual std::optional<size_t> get_offset_by_name(const std::string &/*name*/) const { return std::nullopt; }
};
