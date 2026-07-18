#pragma once

#include "hex/hex_highlighter.h"
#include <string>
#include <vector>

class png_hex_highlighter : public hex_highlighter
{
      public:
	png_hex_highlighter() = default;
	~png_hex_highlighter() override = default;

	bool can_handle(const std::vector<uint8_t> &data) const override;
	bool parse(const std::vector<uint8_t> &data) override;
	highlight_info get_info(const std::vector<uint8_t> &data, size_t offset) const override;
	size_t get_next_symbol_offset(size_t current_offset) const override;
	std::optional<size_t> get_offset_by_name(const std::string &name) const override;

      private:
	struct parsed_chunk {
		size_t offset{0};
		size_t length{0}; // data length
		std::string type;
	};

	std::vector<parsed_chunk> chunks_;
	bool parsed_successfully_{false};
};
