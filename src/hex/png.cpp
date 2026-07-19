#include "hex/png.h"
#include "mime.h"
#include <format>

bool png_hex_highlighter::can_handle(const std::vector<uint8_t> &data) const
{
	if (data.size() < 8)
		return false;
	return data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47 &&
	       data[4] == 0x0D && data[5] == 0x0A && data[6] == 0x1A && data[7] == 0x0A;
}

bool png_hex_highlighter::parse(const std::vector<uint8_t> &data)
{
	parsed_successfully_ = false;
	chunks_.clear();
	has_ihdr_ = false;
	width_ = 0;
	height_ = 0;

	if (!can_handle(data))
		return false;

	// Query MIME type and description using central helpers
	std::string_view view(reinterpret_cast<const char*>(data.data()), data.size());
	mime_type_ = mime::detect_buffer_type(view);
	description_ = mime::detect_buffer_description(view);

	size_t offset = 8;
	while (offset + 12 <= data.size()) {
		uint32_t len = (data[offset] << 24) | (data[offset + 1] << 16) |
			       (data[offset + 2] << 8) | data[offset + 3];
		
		std::string type;
		for (int i = 0; i < 4; ++i) {
			type.push_back(static_cast<char>(data[offset + 4 + i]));
		}

		parsed_chunk chunk;
		chunk.offset = offset;
		chunk.length = len;
		chunk.type = type;
		chunks_.push_back(chunk);

		if (type == "IHDR" && len >= 13 && offset + 21 <= data.size()) {
			size_t data_start = offset + 8;
			width_ = (data[data_start] << 24) | (data[data_start + 1] << 16) |
			         (data[data_start + 2] << 8) | data[data_start + 3];
			height_ = (data[data_start + 4] << 24) | (data[data_start + 5] << 16) |
			          (data[data_start + 6] << 8) | data[data_start + 7];
			bit_depth_ = data[data_start + 8];
			color_type_ = data[data_start + 9];
			compression_ = data[data_start + 10];
			filter_ = data[data_start + 11];
			interlace_ = data[data_start + 12];
			has_ihdr_ = true;
		}

		if (type == "IEND") {
			break;
		}

		offset += 12 + len;

		if (12 + len == 0 || offset < chunk.offset) {
			break;
		}
	}

	parsed_successfully_ = true;
	return true;
}

highlight_info png_hex_highlighter::get_info(const std::vector<uint8_t> &data, size_t offset) const
{
	if (!parsed_successfully_)
		return {};

	if (offset < 8) {
		highlight_info info;
		info.type = hex_semantic_type::magic;
		info.description = "PNG File Signature";
		info.range_start = 0;
		info.range_size = 8;
		return info;
	}

	for (const auto &chunk : chunks_) {
		size_t chunk_end = chunk.offset + 12 + chunk.length;
		if (offset >= chunk.offset && offset < chunk_end) {
			highlight_info info;
			
			if (offset >= chunk.offset && offset < chunk.offset + 4) {
				info.type = hex_semantic_type::file_header;
				info.description = std::format("PNG Chunk \"{}\": Length = {} bytes", chunk.type, chunk.length);
				info.range_start = chunk.offset;
				info.range_size = 4;
				return info;
			}
			if (offset >= chunk.offset + 4 && offset < chunk.offset + 8) {
				info.type = hex_semantic_type::sect_header;
				info.description = std::format("PNG Chunk \"{}\": Type", chunk.type);
				info.range_start = chunk.offset + 4;
				info.range_size = 4;
				return info;
			}
			if (offset >= chunk.offset + 8 && offset < chunk.offset + 8 + chunk.length) {
				info.type = hex_semantic_type::code_section;
				info.range_start = chunk.offset + 8;
				info.range_size = chunk.length;

				if (chunk.type == "IHDR" && chunk.length >= 13 && chunk.offset + 21 <= data.size()) {
					size_t data_start = chunk.offset + 8;
					uint32_t width = (data[data_start] << 24) | (data[data_start + 1] << 16) |
							 (data[data_start + 2] << 8) | data[data_start + 3];
					uint32_t height = (data[data_start + 4] << 24) | (data[data_start + 5] << 16) |
							  (data[data_start + 6] << 8) | data[data_start + 7];
					uint8_t bit_depth = data[data_start + 8];
					uint8_t color_type = data[data_start + 9];
					uint8_t compression = data[data_start + 10];
					uint8_t filter = data[data_start + 11];
					uint8_t interlace = data[data_start + 12];

					info.description = std::format("PNG IHDR Data: Width = {}, Height = {}, Depth = {}, ColorType = {}, Comp = {}, Filter = {}, Interlace = {}",
									width, height, bit_depth, color_type, compression, filter, interlace);
				} else {
					info.description = std::format("PNG Chunk \"{}\" Data ({} bytes)", chunk.type, chunk.length);
				}
				return info;
			}
			if (offset >= chunk.offset + 8 + chunk.length && offset < chunk_end) {
				info.type = hex_semantic_type::symtab_section;
				info.description = std::format("PNG Chunk \"{}\": CRC", chunk.type);
				info.range_start = chunk.offset + 8 + chunk.length;
				info.range_size = 4;
				return info;
			}
		}
	}

	return {hex_semantic_type::normal, ""};
}

size_t png_hex_highlighter::get_next_symbol_offset(size_t current_offset) const
{
	for (const auto &chunk : chunks_) {
		if (chunk.offset > current_offset) {
			return chunk.offset;
		}
	}
	return current_offset;
}

std::optional<size_t> png_hex_highlighter::get_offset_by_name(const std::string &name) const
{
	if (!parsed_successfully_) {
		return std::nullopt;
	}
	for (const auto &chunk : chunks_) {
		if (chunk.type == name) {
			return chunk.offset;
		}
	}
	return std::nullopt;
}

std::string png_hex_highlighter::get_structure_summary() const
{
	if (!parsed_successfully_ || chunks_.empty()) {
		return "";
	}

	std::string summary = "### PNG Structural Overview\n\n";

	// Add Metadata Table
	summary += "#### File Metadata\n\n";
	summary += "| Property | Value |\n";
	summary += "| --- | --- |\n";
	summary += std::format("| **MIME Type** | {} |\n", mime_type_);
	summary += std::format("| **Description** | {} |\n", description_);
	if (has_ihdr_) {
		summary += std::format("| **Dimensions** | {} x {} |\n", width_, height_);
		std::string color_space = "Unknown";
		if (color_type_ == 0) color_space = std::format("Grayscale ({} bit)", bit_depth_);
		else if (color_type_ == 2) color_space = std::format("RGB ({} bit)", bit_depth_);
		else if (color_type_ == 3) color_space = std::format("Palette Index ({} bit)", bit_depth_);
		else if (color_type_ == 4) color_space = std::format("Grayscale + Alpha ({} bit)", bit_depth_);
		else if (color_type_ == 6) color_space = std::format("RGBA ({} bit)", bit_depth_);
		summary += std::format("| **Color Space** | {} |\n", color_space);
		summary += std::format("| **Interlace Method** | {} |\n", interlace_ == 0 ? "None (Standard)" : "Adam7");
	}
	summary += "\n";

	summary += "#### PNG Chunks\n\n";
	summary += "| Type | Offset | Data Size (Bytes) | Total Chunk Size (Bytes) |\n";
	summary += "| :---: | :---: | :---: | :---: |\n";
	for (const auto &chunk : chunks_) {
		summary += std::format("| `{}` | `0x{:X}` | `{}` | `{}` |\n",
		                       chunk.type, chunk.offset, chunk.length, chunk.length + 12);
	}
	summary += "\n";
	return summary;
}
