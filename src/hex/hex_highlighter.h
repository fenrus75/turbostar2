#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
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
| zip_hex_highlighter | src/hex/zip.h   |
| pdf_hex_highlighter | src/hex/pdf.h   |
| tar_hex_highlighter | src/hex/tar.h   |

*/

class hex_highlighter
{
      public:
	virtual ~hex_highlighter() = default;

	// Determine if this highlighter should be used for this data
	virtual bool can_handle(std::span<const uint8_t> data) const = 0;

	// Parse the data and cache structure offsets
	virtual bool parse(std::span<const uint8_t> data) = 0;

	// Query information for a byte offset
	virtual highlight_info get_info(std::span<const uint8_t> data, size_t offset) const = 0;

	// Query the next symbol boundary offset after the current_offset
	virtual size_t get_next_symbol_offset(size_t current_offset) const { return current_offset; }

	// Query offset by name (e.g. section name like ".text", symbol name, or chunk name like "PLTE")
	virtual std::optional<size_t> get_offset_by_name(std::string_view /*name*/) const { return std::nullopt; }

	// Get a high-level overview of the entire parsed structure (e.g. archive content lists)
	virtual std::string get_structure_summary() const { return ""; }

	// Determine if the structural summary should be written directly to a tmp:// file rather than showing directly.
	virtual bool prefer_summary_in_tmp_only() const { return false; }
};
