#pragma once

#include "hex/hex_highlighter.h"
#include <string>
#include <vector>
#include <optional>

class pdf_hex_highlighter : public hex_highlighter
{
      public:
	struct pdf_object {
		std::string id;
		size_t offset{0};
		size_t size{0};
		size_t stream_offset{0};
		size_t stream_size{0};
	};

	struct pdf_xref {
		size_t offset{0};
		size_t size{0};
	};

	struct pdf_trailer {
		size_t offset{0};
		size_t size{0};
	};

	pdf_hex_highlighter() = default;
	~pdf_hex_highlighter() override = default;

	bool can_handle(const std::vector<uint8_t> &data) const override;
	bool parse(const std::vector<uint8_t> &data) override;
	highlight_info get_info(const std::vector<uint8_t> &data, size_t offset) const override;
	size_t get_next_symbol_offset(size_t current_offset) const override;
	std::optional<size_t> get_offset_by_name(const std::string &name) const override;
	std::string get_structure_summary() const override;
	bool prefer_summary_in_tmp_only() const override { return true; }

      private:
	std::vector<pdf_object> objects_;
	std::vector<pdf_xref> xrefs_;
	std::vector<pdf_trailer> trailers_;
	size_t header_size_{0};
	bool parsed_successfully_{false};
	std::string mime_type_;
	std::string description_;
};
