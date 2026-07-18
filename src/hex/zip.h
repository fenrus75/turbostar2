#pragma once

#include "hex/hex_highlighter.h"
#include <string>
#include <vector>
#include <optional>

class zip_hex_highlighter : public hex_highlighter
{
      public:
	struct parsed_local_file {
		std::string filename;
		size_t lfh_offset{0};
		size_t header_size{0};
		size_t data_offset{0};
		size_t compressed_size{0};
		size_t uncompressed_size{0};
	};

	struct parsed_cd_entry {
		std::string filename;
		size_t offset{0};
		size_t entry_size{0};
	};

	zip_hex_highlighter() = default;
	~zip_hex_highlighter() override = default;

	bool can_handle(const std::vector<uint8_t> &data) const override;
	bool parse(const std::vector<uint8_t> &data) override;
	highlight_info get_info(const std::vector<uint8_t> &data, size_t offset) const override;
	size_t get_next_symbol_offset(size_t current_offset) const override;
	std::optional<size_t> get_offset_by_name(const std::string &name) const override;

	const std::vector<parsed_local_file> &get_local_files() const { return local_files_; }
	const std::vector<parsed_cd_entry> &get_cd_entries() const { return cd_entries_; }

      private:
	std::vector<parsed_local_file> local_files_;
	std::vector<parsed_cd_entry> cd_entries_;
	size_t eocd_offset_{0};
	size_t eocd_size_{0};
	bool parsed_successfully_{false};
};
