#include "hex/jpeg.h"
#include "mime.h"
#include <format>

namespace
{
std::string get_marker_name(uint8_t code)
{
	if (code == 0xD8) return "SOI";
	if (code == 0xD9) return "EOI";
	if (code >= 0xE0 && code <= 0xEF) return std::format("APP{}", code - 0xE0);
	if (code == 0xDB) return "DQT";
	if (code == 0xC4) return "DHT";
	if (code == 0xC0) return "SOF0";
	if (code == 0xC2) return "SOF2";
	if (code >= 0xC1 && code <= 0xCF) return std::format("SOF{}", code - 0xC0);
	if (code == 0xDA) return "SOS";
	if (code == 0xDD) return "DRI";
	if (code == 0xFE) return "COM";
	if (code >= 0xD0 && code <= 0xD7) return std::format("RST{}", code - 0xD0);
	if (code == 0x01) return "TEM";
	return std::format("Marker 0x{:02X}", code);
}
} // namespace

bool jpeg_hex_highlighter::can_handle(std::span<const uint8_t> data) const
{
	if (data.size() < 4)
		return false;
	return data[0] == 0xFF && data[1] == 0xD8;
}

bool jpeg_hex_highlighter::parse(std::span<const uint8_t> data)
{
	parsed_successfully_ = false;
	markers_.clear();
	has_sof_ = false;
	width_ = 0;
	height_ = 0;

	if (!can_handle(data))
		return false;

	// Query MIME type and description using central helpers
	std::string_view view(reinterpret_cast<const char*>(data.data()), data.size());
	mime_type_ = mime::detect_buffer_type(view);
	description_ = mime::detect_buffer_description(view);

	// Add SOI
	parsed_marker soi;
	soi.offset = 0;
	soi.length = 2;
	soi.marker_code = 0xD8;
	soi.name = "SOI";
	markers_.push_back(soi);

	size_t offset = 2;
	while (offset + 2 <= data.size()) {
		// Skip leading 0xFF padding bytes
		if (data[offset] != 0xFF) {
			break;
		}
		
		size_t marker_start = offset;
		while (offset < data.size() && data[offset] == 0xFF) {
			offset++;
		}
		
		if (offset >= data.size()) {
			break;
		}
		
		uint8_t marker_code = data[offset];
		offset++; // now points to after the marker byte
		
		parsed_marker pm;
		pm.offset = marker_start;
		pm.marker_code = marker_code;
		pm.name = get_marker_name(marker_code);
		
		bool is_standalone = (marker_code == 0xD8 || marker_code == 0xD9 || marker_code == 0x01 || (marker_code >= 0xD0 && marker_code <= 0xD7));
		
		if (is_standalone) {
			pm.length = offset - marker_start;
			markers_.push_back(pm);
			if (marker_code == 0xD9) { // EOI
				break;
			}
			continue;
		}
		
		if (offset + 2 > data.size()) {
			break;
		}
		
		uint16_t length_val = (data[offset] << 8) | data[offset + 1];
		pm.length = (offset - marker_start) + length_val;
		markers_.push_back(pm);

		if ((marker_code == 0xC0 || marker_code == 0xC2) && pm.length >= 10 && pm.offset + 10 <= data.size()) {
			size_t data_start = pm.offset + 4;
			precision_ = data[data_start];
			height_ = (data[data_start + 1] << 8) | data[data_start + 2];
			width_ = (data[data_start + 3] << 8) | data[data_start + 4];
			components_ = data[data_start + 5];
			has_sof_ = true;
		}
		
		offset += length_val;
		
		if (marker_code == 0xDA) { // SOS
			size_t scan_data_start = offset;
			size_t next_marker_pos = data.size();
			
			size_t scan_idx = scan_data_start;
			while (scan_idx + 1 < data.size()) {
				if (data[scan_idx] == 0xFF) {
					uint8_t next_b = data[scan_idx + 1];
					if (next_b != 0x00 && next_b != 0xFF && (next_b < 0xD0 || next_b > 0xD7)) {
						next_marker_pos = scan_idx;
						break;
					}
				}
				scan_idx++;
			}
			
			if (next_marker_pos > scan_data_start) {
				parsed_marker scan_pm;
				scan_pm.offset = scan_data_start;
				scan_pm.length = next_marker_pos - scan_data_start;
				scan_pm.marker_code = 0x00; // Special placeholder code for entropy scan data
				scan_pm.name = "Entropy-Coded Image Data";
				markers_.push_back(scan_pm);
			}
			
			offset = next_marker_pos;
		}
	}

	parsed_successfully_ = true;
	return true;
}

highlight_info jpeg_hex_highlighter::get_info(std::span<const uint8_t> data, size_t offset) const
{
	if (!parsed_successfully_)
		return {};

	for (const auto &pm : markers_) {
		size_t segment_end = pm.offset + pm.length;
		if (offset >= pm.offset && offset < segment_end) {
			highlight_info info;
			info.range_start = pm.offset;
			info.range_size = pm.length;

			if (pm.marker_code == 0xD8 || pm.marker_code == 0xD9) {
				info.type = hex_semantic_type::magic;
			} else if (pm.marker_code >= 0xE0 && pm.marker_code <= 0xEF) {
				info.type = hex_semantic_type::file_header;
			} else if (pm.marker_code == 0x00) {
				info.type = hex_semantic_type::code_section; // entropy scan data
			} else {
				info.type = hex_semantic_type::sect_header;
			}

			// Generate descriptive summaries
			if (pm.marker_code == 0xD8) {
				info.description = "JPEG Start of Image (SOI)";
			} else if (pm.marker_code == 0xD9) {
				info.description = "JPEG End of Image (EOI)";
			} else if (pm.marker_code == 0x00) {
				info.description = std::format("JPEG Entropy-Coded Image Scan Data ({} bytes)", pm.length);
			} else if (pm.marker_code == 0xE0 && pm.length >= 18 && pm.offset + 18 <= data.size()) {
				// Parse JFIF APP0 details
				size_t ident_offset = pm.offset + 4;
				if (data[ident_offset] == 'J' && data[ident_offset + 1] == 'F' &&
				    data[ident_offset + 2] == 'I' && data[ident_offset + 3] == 'F' &&
				    data[ident_offset + 4] == 0) {
					uint8_t major = data[ident_offset + 5];
					uint8_t minor = data[ident_offset + 6];
					uint8_t units = data[ident_offset + 7];
					uint16_t x_dens = (data[ident_offset + 8] << 8) | data[ident_offset + 9];
					uint16_t y_dens = (data[ident_offset + 10] << 8) | data[ident_offset + 11];
					uint8_t x_thumb = data[ident_offset + 12];
					uint8_t y_thumb = data[ident_offset + 13];

					std::string unit_str = "Aspect Ratio";
					if (units == 1) unit_str = "Pixels/inch";
					else if (units == 2) unit_str = "Pixels/cm";

					info.description = std::format("JFIF APP0: Ver {}.{:02d}, Units = {}, Xdensity = {}, Ydensity = {}, Thumbnail = {}x{}",
									major, minor, unit_str, x_dens, y_dens, x_thumb, y_thumb);
				} else {
					info.description = std::format("JPEG APP0 Segment ({} bytes)", pm.length);
				}
			} else if ((pm.marker_code == 0xC0 || pm.marker_code == 0xC2) && pm.length >= 10 && pm.offset + 10 <= data.size()) {
				// Parse SOF image dimensions
				size_t data_start = pm.offset + 4;
				uint8_t precision = data[data_start];
				uint16_t height = (data[data_start + 1] << 8) | data[data_start + 2];
				uint16_t width = (data[data_start + 3] << 8) | data[data_start + 4];
				uint8_t components = data[data_start + 5];

				info.description = std::format("JPEG {} Header: Width = {}, Height = {}, Precision = {}, Components = {}",
								pm.name, width, height, precision, components);
			} else {
				info.description = std::format("JPEG {} Segment ({} bytes)", pm.name, pm.length);
			}

			return info;
		}
	}

	return {hex_semantic_type::normal, ""};
}

size_t jpeg_hex_highlighter::get_next_symbol_offset(size_t current_offset) const
{
	for (const auto &pm : markers_) {
		if (pm.offset > current_offset) {
			return pm.offset;
		}
	}
	return current_offset;
}

std::optional<size_t> jpeg_hex_highlighter::get_offset_by_name(std::string_view name) const
{
	if (!parsed_successfully_) {
		return std::nullopt;
	}
	for (const auto &marker : markers_) {
		if (marker.name == name) {
			return marker.offset;
		}
	}
	return std::nullopt;
}

std::string jpeg_hex_highlighter::get_structure_summary() const
{
	if (!parsed_successfully_ || markers_.empty()) {
		return "";
	}

	std::string summary = "### JPEG Structural Overview\n\n";

	// Add Metadata Table
	summary += "#### File Metadata\n\n";
	summary += "| Property | Value |\n";
	summary += "| --- | --- |\n";
	summary += std::format("| **MIME Type** | {} |\n", mime_type_);
	summary += std::format("| **Description** | {} |\n", description_);
	if (has_sof_) {
		summary += std::format("| **Dimensions** | {} x {} |\n", width_, height_);
		summary += std::format("| **Precision** | {} bits/component |\n", precision_);
		summary += std::format("| **Number of Components** | {} |\n", components_);
	}
	summary += "\n";

	summary += "#### JPEG Markers\n\n";
	summary += "| Marker Name | Marker Code | Offset | Payload Size (Bytes) |\n";
	summary += "| :--- | :---: | :---: | :---: |\n";
	for (const auto &marker : markers_) {
		summary += std::format("| `{}` | `0x{:02X}` | `0x{:X}` | `{}` |\n",
		                       marker.name, marker.marker_code, marker.offset, marker.length);
	}
	summary += "\n";
	return summary;
}
