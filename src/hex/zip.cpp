#include "hex/zip.h"
#include <algorithm>
#include <cstring>
#include <format>

bool zip_hex_highlighter::can_handle(std::span<const uint8_t> data) const
{
	return data.size() >= 4 && data[0] == 0x50 && data[1] == 0x4B && data[2] == 0x03 && data[3] == 0x04;
}

bool zip_hex_highlighter::parse(std::span<const uint8_t> data)
{
	parsed_successfully_ = false;
	local_files_.clear();
	cd_entries_.clear();
	eocd_offset_ = 0;
	eocd_size_ = 0;

	if (!can_handle(data)) {
		return false;
	}

	// 1. Scan backwards from the end of the data to find EOCD
	size_t eocd_pos = 0;
	bool found_eocd = false;
	if (data.size() >= 22) {
		size_t max_scan = std::min(data.size() - 22, static_cast<size_t>(65557));
		for (size_t i = 0; i <= max_scan; ++i) {
			size_t pos = data.size() - 22 - i;
			if (data[pos] == 0x50 && data[pos + 1] == 0x4B && data[pos + 2] == 0x05 && data[pos + 3] == 0x06) {
				eocd_pos = pos;
				found_eocd = true;
				break;
			}
		}
	}

	if (found_eocd) {
		eocd_offset_ = eocd_pos;
		uint16_t comment_len = data[eocd_pos + 20] | (data[eocd_pos + 21] << 8);
		eocd_size_ = 22 + comment_len;

		// Parse Central Directory Offset & Size
		size_t cd_offset = data[eocd_pos + 16] | (data[eocd_pos + 17] << 8) | (data[eocd_pos + 18] << 16) | (data[eocd_pos + 19] << 24);
		size_t cd_size = data[eocd_pos + 12] | (data[eocd_pos + 13] << 8) | (data[eocd_pos + 14] << 16) | (data[eocd_pos + 15] << 24);

		if (cd_offset + cd_size <= data.size()) {
			size_t offset = cd_offset;
			while (offset + 46 <= cd_offset + cd_size && offset + 46 <= data.size()) {
				if (data[offset] != 0x50 || data[offset + 1] != 0x4B || data[offset + 2] != 0x01 || data[offset + 3] != 0x02) {
					break;
				}

				uint16_t filename_len = data[offset + 28] | (data[offset + 29] << 8);
				uint16_t extra_len = data[offset + 30] | (data[offset + 31] << 8);
				uint16_t comment_len_entry = data[offset + 32] | (data[offset + 33] << 8);
				uint32_t local_header_offset = data[offset + 42] | (data[offset + 43] << 8) | (data[offset + 44] << 16) | (data[offset + 45] << 24);

				std::string filename;
				if (offset + 46 + filename_len <= data.size()) {
					filename.assign(reinterpret_cast<const char *>(&data[offset + 46]), filename_len);
				}

				parsed_cd_entry cd;
				cd.filename = filename;
				cd.offset = offset;
				cd.entry_size = 46 + filename_len + extra_len + comment_len_entry;
				cd_entries_.push_back(cd);

				// Parse corresponding LFH
				if (local_header_offset + 30 <= data.size()) {
					size_t lfh = local_header_offset;
					if (data[lfh] == 0x50 && data[lfh + 1] == 0x4B && data[lfh + 2] == 0x03 && data[lfh + 3] == 0x04) {
						uint16_t lfh_filename_len = data[lfh + 26] | (data[lfh + 27] << 8);
						uint16_t lfh_extra_len = data[lfh + 28] | (data[lfh + 29] << 8);
						uint32_t comp_size = data[lfh + 18] | (data[lfh + 19] << 8) | (data[lfh + 20] << 16) | (data[lfh + 21] << 24);
						uint32_t uncomp_size = data[lfh + 22] | (data[lfh + 23] << 8) | (data[lfh + 24] << 16) | (data[lfh + 25] << 24);

						parsed_local_file lf;
						lf.filename = filename;
						lf.lfh_offset = lfh;
						lf.header_size = 30 + lfh_filename_len + lfh_extra_len;
						lf.data_offset = lfh + lf.header_size;
						lf.compressed_size = comp_size;
						lf.uncompressed_size = uncomp_size;

						// Overwrite sizes from CD if LFH sizes are zeroed out (Data Descriptor used)
						if (comp_size == 0 || uncomp_size == 0) {
							uint32_t cd_comp_size = data[offset + 20] | (data[offset + 21] << 8) | (data[offset + 22] << 16) | (data[offset + 23] << 24);
							uint32_t cd_uncomp_size = data[offset + 24] | (data[offset + 25] << 8) | (data[offset + 26] << 16) | (data[offset + 27] << 24);
							lf.compressed_size = cd_comp_size;
							lf.uncompressed_size = cd_uncomp_size;
						}

						local_files_.push_back(lf);
					}
				}

				offset += cd.entry_size;
			}
		}
	} else {
		// Fallback sequential parsing
		size_t offset = 0;
		while (offset + 30 <= data.size()) {
			if (data[offset] == 0x50 && data[offset + 1] == 0x4B && data[offset + 2] == 0x03 && data[offset + 3] == 0x04) {
				uint16_t filename_len = data[offset + 26] | (data[offset + 27] << 8);
				uint16_t extra_len = data[offset + 28] | (data[offset + 29] << 8);
				uint32_t comp_size = data[offset + 18] | (data[offset + 19] << 8) | (data[offset + 20] << 16) | (data[offset + 21] << 24);
				uint32_t uncomp_size = data[offset + 22] | (data[offset + 23] << 8) | (data[offset + 24] << 16) | (data[offset + 25] << 24);

				std::string filename;
				if (offset + 30 + filename_len <= data.size()) {
					filename.assign(reinterpret_cast<const char *>(&data[offset + 30]), filename_len);
				}

				parsed_local_file lf;
				lf.filename = filename;
				lf.lfh_offset = offset;
				lf.header_size = 30 + filename_len + extra_len;
				lf.data_offset = offset + lf.header_size;
				lf.compressed_size = comp_size;
				lf.uncompressed_size = uncomp_size;
				local_files_.push_back(lf);

				offset += lf.header_size + comp_size;
			} else if (data[offset] == 0x50 && data[offset + 1] == 0x4B && data[offset + 2] == 0x01 && data[offset + 3] == 0x02) {
				uint16_t filename_len = data[offset + 28] | (data[offset + 29] << 8);
				uint16_t extra_len = data[offset + 30] | (data[offset + 31] << 8);
				uint16_t comment_len_entry = data[offset + 32] | (data[offset + 33] << 8);

				std::string filename;
				if (offset + 46 + filename_len <= data.size()) {
					filename.assign(reinterpret_cast<const char *>(&data[offset + 46]), filename_len);
				}

				parsed_cd_entry cd;
				cd.filename = filename;
				cd.offset = offset;
				cd.entry_size = 46 + filename_len + extra_len + comment_len_entry;
				cd_entries_.push_back(cd);

				offset += cd.entry_size;
			} else if (data[offset] == 0x50 && data[offset + 1] == 0x4B && data[offset + 2] == 0x05 && data[offset + 3] == 0x06) {
				uint16_t comment_len = data[offset + 20] | (data[offset + 21] << 8);
				eocd_offset_ = offset;
				eocd_size_ = 22 + comment_len;
				break;
			} else {
				offset++;
			}
		}
	}

	parsed_successfully_ = !local_files_.empty() || eocd_offset_ > 0;
	return parsed_successfully_;
}

highlight_info zip_hex_highlighter::get_info(std::span<const uint8_t> data, size_t offset) const
{
	(void)data;
	if (!parsed_successfully_) {
		return {hex_semantic_type::normal, "", 0, 0};
	}

	// Check local files
	for (const auto &lf : local_files_) {
		if (offset >= lf.lfh_offset && offset < lf.data_offset) {
			return {hex_semantic_type::file_header, "ZIP Local File Header: " + lf.filename, lf.lfh_offset, lf.header_size};
		}
		if (offset >= lf.data_offset && offset < lf.data_offset + lf.compressed_size) {
			return {hex_semantic_type::data_section, std::format("ZIP Compressed Data: {} ({} bytes)", lf.filename, lf.compressed_size), lf.data_offset, lf.compressed_size};
		}
	}

	// Check Central Directory entries
	for (const auto &cd : cd_entries_) {
		if (offset >= cd.offset && offset < cd.offset + cd.entry_size) {
			return {hex_semantic_type::sect_header, "ZIP Central Directory Entry: " + cd.filename, cd.offset, cd.entry_size};
		}
	}

	// Check EOCD
	if (eocd_offset_ > 0 && offset >= eocd_offset_ && offset < eocd_offset_ + eocd_size_) {
		return {hex_semantic_type::magic, "ZIP End of Central Directory Record", eocd_offset_, eocd_size_};
	}

	return {hex_semantic_type::normal, "", 0, 0};
}

size_t zip_hex_highlighter::get_next_symbol_offset(size_t current_offset) const
{
	std::vector<size_t> offsets;
	for (const auto &lf : local_files_) {
		offsets.push_back(lf.lfh_offset);
		offsets.push_back(lf.data_offset);
	}
	for (const auto &cd : cd_entries_) {
		offsets.push_back(cd.offset);
	}
	if (eocd_offset_ > 0) {
		offsets.push_back(eocd_offset_);
	}
	std::sort(offsets.begin(), offsets.end());
	for (size_t off : offsets) {
		if (off > current_offset) {
			return off;
		}
	}
	return current_offset;
}

std::optional<size_t> zip_hex_highlighter::get_offset_by_name(std::string_view name) const
{
	if (!parsed_successfully_) {
		return std::nullopt;
	}
	for (const auto &lf : local_files_) {
		if (lf.filename == name) {
			return lf.lfh_offset;
		}
	}
	return std::nullopt;
}

std::string zip_hex_highlighter::get_structure_summary() const
{
	if (!parsed_successfully_ || local_files_.empty()) {
		return "";
	}

	std::string summary = "### Archive Contents (ZIP)\n\n"
	                      "| Filename | Header Offset | Data Offset | Compressed Size | Uncompressed Size |\n"
	                      "| :--- | :---: | :---: | :---: | :---: |\n";

	for (const auto &lf : local_files_) {
		summary += std::format("| `{}` | `0x{:X}` | `0x{:X}` | `{}` | `{}` |\n",
		                       lf.filename, lf.lfh_offset, lf.data_offset, lf.compressed_size, lf.uncompressed_size);
	}
	summary += "\n";
	return summary;
}
