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
};

// ELF implementation
class elf_hex_highlighter : public hex_highlighter
{
      public:
	elf_hex_highlighter() = default;
	~elf_hex_highlighter() override = default;

	bool can_handle(const std::vector<uint8_t> &data) const override;
	bool parse(const std::vector<uint8_t> &data) override;
	highlight_info get_info(const std::vector<uint8_t> &data, size_t offset) const override;

      private:
	struct elf_parsed_data {
		bool is_64{false};
		bool is_lsb{true};
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

	struct parsed_section {
		size_t index{0};
		std::string name;
		uint32_t type_val{0};
		uint64_t offset{0};
		uint64_t size{0};
		hex_semantic_type semantic{hex_semantic_type::normal};
	};

	elf_parsed_data header_;
	std::vector<parsed_section> sections_;
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
