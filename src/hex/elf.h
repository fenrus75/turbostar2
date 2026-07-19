#pragma once

#include "hex/hex_highlighter.h"
#include <string>
#include <vector>

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
	std::optional<size_t> get_offset_by_name(const std::string &name) const override;
	std::string get_structure_summary() const override;
	bool prefer_summary_in_tmp_only() const override { return true; }

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
