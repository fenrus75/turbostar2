#pragma once

#include "hex/hex_highlighter.h"
#include <string>
#include <vector>
#include <optional>

class tar_hex_highlighter : public hex_highlighter
{
      public:
	struct parsed_file {
		std::string filename;
		size_t header_offset{0};
		size_t data_offset{0};
		size_t size{0};
	};

	tar_hex_highlighter() = default;
	~tar_hex_highlighter() override = default;

	bool can_handle(std::span<const uint8_t> data) const override;
	bool parse(std::span<const uint8_t> data) override;
	highlight_info get_info(std::span<const uint8_t> data, size_t offset) const override;
	size_t get_next_symbol_offset(size_t current_offset) const override;
	std::optional<size_t> get_offset_by_name(std::string_view name) const override;
	std::string get_structure_summary() const override;

	const std::vector<parsed_file> &get_files() const { return files_; }

      private:
	std::vector<parsed_file> files_;
	bool parsed_successfully_{false};
};
