#include "hex/tar.h"
#include <algorithm>
#include <cstring>
#include <format>
#include <string_view>

bool tar_hex_highlighter::can_handle(const std::vector<uint8_t> &data) const
{
	if (data.size() < 512) {
		return false;
	}
	// Check for "ustar" magic at offset 257
	return std::string_view(reinterpret_cast<const char*>(data.data() + 257), 5) == "ustar";
}

bool tar_hex_highlighter::parse(const std::vector<uint8_t> &data)
{
	parsed_successfully_ = false;
	files_.clear();

	if (!can_handle(data)) {
		return false;
	}

	size_t offset = 0;
	while (offset + 512 <= data.size()) {
		// If the block is entirely zero, it is the end of the archive marker
		bool all_zero = true;
		for (size_t i = 0; i < 512; ++i) {
			if (data[offset + i] != 0) {
				all_zero = false;
				break;
			}
		}
		if (all_zero) {
			break;
		}

		// Parse filename (first 100 bytes of the block)
		std::string filename;
		for (size_t i = 0; i < 100; ++i) {
			if (data[offset + i] == '\0') {
				break;
			}
			filename.push_back(static_cast<char>(data[offset + i]));
		}

		// Also check prefix (offset 345, 155 bytes) if present to construct the full path
		std::string prefix;
		if (std::string_view(reinterpret_cast<const char*>(data.data() + offset + 257), 5) == "ustar") {
			for (size_t i = 0; i < 155; ++i) {
				if (data[offset + 345 + i] == '\0') {
					break;
				}
				prefix.push_back(static_cast<char>(data[offset + 345 + i]));
			}
		}
		if (!prefix.empty()) {
			filename = prefix + "/" + filename;
		}

		// Parse octal size at offset 124 (12 bytes)
		size_t size = 0;
		std::string size_str;
		for (size_t i = 0; i < 12; ++i) {
			char c = static_cast<char>(data[offset + 124 + i]);
			if (c == '\0' || c == ' ') {
				continue;
			}
			if (c >= '0' && c <= '7') {
				size_str.push_back(c);
			}
		}
		if (!size_str.empty()) {
			try {
				size = std::stoull(size_str, nullptr, 8);
			} catch (...) {
				size = 0;
			}
		}

		parsed_file pf;
		pf.filename = filename;
		pf.header_offset = offset;
		pf.data_offset = offset + 512;
		pf.size = size;
		files_.push_back(pf);

		// Tar padding: each file is padded to 512-byte boundaries
		size_t padded_size = (size + 511) / 512 * 512;
		offset += 512 + padded_size;
	}

	parsed_successfully_ = !files_.empty();
	return parsed_successfully_;
}

highlight_info tar_hex_highlighter::get_info(const std::vector<uint8_t> &data, size_t offset) const
{
	highlight_info info;
	info.type = hex_semantic_type::normal;
	info.range_start = offset;
	info.range_size = 1;

	if (!parsed_successfully_) {
		return info;
	}

	// Check if the offset falls into any parsed file header or data section
	for (const auto &file : files_) {
		// Header block (512 bytes)
		if (offset >= file.header_offset && offset < file.header_offset + 512) {
			info.type = hex_semantic_type::file_header;
			info.description = std::format("TAR Header: {} (size: {} bytes)", file.filename, file.size);
			info.range_start = file.header_offset;
			info.range_size = 512;
			return info;
		}
		// Data section
		if (file.size > 0 && offset >= file.data_offset && offset < file.data_offset + file.size) {
			info.type = hex_semantic_type::data_section;
			info.description = std::format("TAR File Data: {}", file.filename);
			info.range_start = file.data_offset;
			info.range_size = file.size;
			return info;
		}
		// Padding section after data (up to next 512-byte boundary)
		size_t padded_size = (file.size + 511) / 512 * 512;
		if (padded_size > file.size && offset >= file.data_offset + file.size && offset < file.data_offset + padded_size) {
			info.type = hex_semantic_type::normal;
			info.description = std::format("TAR Padding: {}", file.filename);
			info.range_start = file.data_offset + file.size;
			info.range_size = padded_size - file.size;
			return info;
		}
	}

	// Check if it's the EOD (two consecutive zero blocks)
	if (!files_.empty()) {
		const auto &last_file = files_.back();
		size_t padded_size = (last_file.size + 511) / 512 * 512;
		size_t eod_start = last_file.data_offset + padded_size;
		if (offset >= eod_start && offset < data.size()) {
			info.type = hex_semantic_type::magic;
			info.description = "TAR End of Archive Marker";
			info.range_start = eod_start;
			info.range_size = data.size() - eod_start;
			return info;
		}
	}

	return info;
}

size_t tar_hex_highlighter::get_next_symbol_offset(size_t current_offset) const
{
	if (!parsed_successfully_) {
		return current_offset;
	}

	for (const auto &file : files_) {
		if (current_offset < file.header_offset) {
			return file.header_offset;
		}
		if (current_offset < file.data_offset) {
			return file.data_offset;
		}
		size_t padded_size = (file.size + 511) / 512 * 512;
		if (current_offset < file.data_offset + padded_size) {
			return file.data_offset + padded_size;
		}
	}
	return current_offset;
}

std::optional<size_t> tar_hex_highlighter::get_offset_by_name(const std::string &name) const
{
	if (!parsed_successfully_) {
		return std::nullopt;
	}

	for (const auto &file : files_) {
		if (file.filename == name) {
			return file.data_offset;
		}
	}
	return std::nullopt;
}

std::string tar_hex_highlighter::get_structure_summary() const
{
	if (!parsed_successfully_ || files_.empty()) {
		return "";
	}

	std::string summary = "### Archive Contents (TAR)\n\n"
	                      "| Filename | Header Offset | Data Offset | File Size (Bytes) |\n"
	                      "| :--- | :---: | :---: | :---: |\n";

	for (const auto &file : files_) {
		summary += std::format("| `{}` | `0x{:X}` | `0x{:X}` | `{}` |\n",
		                       file.filename, file.header_offset, file.data_offset, file.size);
	}
	summary += "\n";
	return summary;
}
