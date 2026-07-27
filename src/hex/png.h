#pragma once

#include "hex/hex_highlighter.h"
#include <string>
#include <vector>

class png_hex_highlighter : public hex_highlighter
{
      public:
	png_hex_highlighter() = default;
	~png_hex_highlighter() override = default;

	bool can_handle(std::span<const uint8_t> data) const override;
	bool parse(std::span<const uint8_t> data) override;
	highlight_info get_info(std::span<const uint8_t> data, size_t offset) const override;
	size_t get_next_symbol_offset(size_t current_offset) const override;
	std::optional<size_t> get_offset_by_name(std::string_view name) const override;
	std::string get_structure_summary() const override;
	bool prefer_summary_in_tmp_only() const override { return true; }

      private:
	struct parsed_chunk {
		size_t offset{0};
		size_t length{0}; // data length
		std::string type;
	};

	std::vector<parsed_chunk> chunks_;
	bool parsed_successfully_{false};
	std::string mime_type_;
	std::string description_;
	uint32_t width_{0};
	uint32_t height_{0};
	uint8_t bit_depth_{0};
	uint8_t color_type_{0};
	uint8_t compression_{0};
	uint8_t filter_{0};
	uint8_t interlace_{0};
	bool has_ihdr_{false};
};
