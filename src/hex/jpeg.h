#pragma once

#include "hex/hex_highlighter.h"
#include <string>
#include <vector>

class jpeg_hex_highlighter : public hex_highlighter
{
      public:
	struct parsed_marker {
		size_t offset{0};
		size_t length{0}; // total size in bytes (marker + payload)
		uint8_t marker_code{0};
		std::string name;
	};

	jpeg_hex_highlighter() = default;
	~jpeg_hex_highlighter() override = default;

	bool can_handle(const std::vector<uint8_t> &data) const override;
	bool parse(const std::vector<uint8_t> &data) override;
	highlight_info get_info(const std::vector<uint8_t> &data, size_t offset) const override;
	size_t get_next_symbol_offset(size_t current_offset) const override;

      private:
	std::vector<parsed_marker> markers_;
	bool parsed_successfully_{false};
};
