#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

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
};

// ELF implementation
class elf_hex_highlighter : public hex_highlighter
{
      public:
	struct parsed_section {
		size_t index{0};
		std::string name;
		uint32_t type_val{0};
		uint64_t offset{0};
		uint64_t size{0};
		hex_semantic_type semantic{hex_semantic_type::normal};
	};

	struct parsed_symbol {
		std::string name;
		uint64_t offset{0};
		uint64_t size{0};
	};

	elf_hex_highlighter() = default;
	~elf_hex_highlighter() override = default;

	bool can_handle(const std::vector<uint8_t> &data) const override;
	bool parse(const std::vector<uint8_t> &data) override;
	highlight_info get_info(const std::vector<uint8_t> &data, size_t offset) const override;
	size_t get_next_symbol_offset(size_t current_offset) const override;

	const std::vector<parsed_section> &get_sections() const { return sections_; }
	const std::vector<parsed_symbol> &get_symbols() const { return symbols_; }

      private:
	struct elf_parsed_data {
		bool is_64{false};
		bool is_lsb{true};
		uint16_t e_machine{0};
		uint64_t e_entry{0};
		uint64_t e_phoff{0};
		uint64_t e_shoff{0};
		uint16_t e_phnum{0};
		uint16_t e_phentsize{0};
		uint16_t e_shnum{0};
		uint16_t e_shentsize{0};
		uint16_t e_shstrndx{0};
		uint16_t e_ehsize{0};
	};

	elf_parsed_data header_;
	std::vector<parsed_section> sections_;
	std::vector<parsed_symbol> symbols_;
	bool parsed_successfully_{false};
};

// PNG implementation
class png_hex_highlighter : public hex_highlighter
{
      public:
	png_hex_highlighter() = default;
	~png_hex_highlighter() override = default;

	bool can_handle(const std::vector<uint8_t> &data) const override;
	bool parse(const std::vector<uint8_t> &data) override;
	highlight_info get_info(const std::vector<uint8_t> &data, size_t offset) const override;
	size_t get_next_symbol_offset(size_t current_offset) const override;

      private:
	struct parsed_chunk {
		size_t offset{0};
		size_t length{0}; // data length
		std::string type;
	};

	std::vector<parsed_chunk> chunks_;
	bool parsed_successfully_{false};
};

// Singleton registry of highlighters
class hex_highlighter_registry
{
      public:
	static hex_highlighter_registry &get_instance();

	// Selects the appropriate highlighter (returns nullptr if none match)
	std::shared_ptr<hex_highlighter> detect_highlighter(const std::vector<uint8_t> &data) const;

      private:
	hex_highlighter_registry();
	~hex_highlighter_registry() = default;

	std::vector<std::shared_ptr<hex_highlighter>> highlighters_;
};
