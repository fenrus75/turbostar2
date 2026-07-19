#include "hex/pdf.h"
#include "mime.h"
#include <algorithm>
#include <cstring>
#include <format>

static bool is_whitespace(uint8_t c) {
    return c == 0 || c == '\t' || c == '\n' || c == '\f' || c == '\r' || c == ' ';
}

static bool is_digit(uint8_t c) {
    return c >= '0' && c <= '9';
}

bool pdf_hex_highlighter::can_handle(const std::vector<uint8_t> &data) const {
	return data.size() >= 5 && data[0] == '%' && data[1] == 'P' && data[2] == 'D' && data[3] == 'F' && data[4] == '-';
}

bool pdf_hex_highlighter::parse(const std::vector<uint8_t> &data) {
	parsed_successfully_ = false;
	objects_.clear();
	xrefs_.clear();
	trailers_.clear();
	header_size_ = 0;

	if (!can_handle(data)) {
		return false;
	}

	// Query MIME type and description using central helpers
	std::string_view view(reinterpret_cast<const char*>(data.data()), data.size());
	mime_type_ = mime::detect_buffer_type(view);
	description_ = mime::detect_buffer_description(view);

	size_t offset = 0;
	while (offset < data.size() && data[offset] != '\n' && data[offset] != '\r') {
		offset++;
	}
	while (offset < data.size() && (data[offset] == '\n' || data[offset] == '\r')) {
		offset++;
	}
	header_size_ = offset;

	auto match_str = [&data](size_t pos, const char* str) -> bool {
		size_t len = strlen(str);
		if (pos + len > data.size()) return false;
		for (size_t i = 0; i < len; ++i) {
			if (data[pos + i] != (uint8_t)str[i]) return false;
		}
		return true;
	};

	pdf_object current_obj;
	bool in_obj = false;
	bool in_stream = false;

	pdf_xref current_xref;
	bool in_xref = false;

	pdf_trailer current_trailer;
	bool in_trailer = false;

	size_t i = 0;
	while (i < data.size()) {
		if (!in_obj && !in_xref && !in_trailer && i + 6 <= data.size() && 
		    match_str(i, " obj") && (i == 0 || is_whitespace(data[i-1]) || is_digit(data[i-1]))) {
			
			size_t start = i;
			while (start > 0 && is_whitespace(data[start-1])) start--;
			while (start > 0 && is_digit(data[start-1])) start--; // gen
			while (start > 0 && is_whitespace(data[start-1])) start--;
			while (start > 0 && is_digit(data[start-1])) start--; // id
			
			if (start < i) {
				current_obj = pdf_object{};
				current_obj.offset = start;
				
				std::string raw_id;
				for (size_t k = start; k < i; ++k) {
					if (is_whitespace(data[k])) {
						if (!raw_id.empty() && raw_id.back() != ' ') {
							raw_id += ' ';
						}
					} else {
						raw_id += (char)data[k];
					}
				}
				while (!raw_id.empty() && raw_id.back() == ' ') {
					raw_id.pop_back();
				}
				current_obj.id = raw_id;
				in_obj = true;
			}
		}

		if (in_obj && !in_stream && match_str(i, "stream")) {
			current_obj.stream_offset = i;
			in_stream = true;
		}
		if (in_obj && in_stream && match_str(i, "endstream")) {
			current_obj.stream_size = (i + 9) - current_obj.stream_offset;
			in_stream = false;
		}
		if (in_obj && !in_stream && match_str(i, "endobj")) {
			current_obj.size = (i + 6) - current_obj.offset;
			objects_.push_back(current_obj);
			in_obj = false;
		}

		if (!in_obj && !in_xref && !in_trailer && match_str(i, "xref")) {
			current_xref = pdf_xref{};
			current_xref.offset = i;
			in_xref = true;
		}

		if (match_str(i, "trailer")) {
			if (in_xref) {
				current_xref.size = i - current_xref.offset;
				xrefs_.push_back(current_xref);
				in_xref = false;
			}
			if (!in_obj) {
				current_trailer = pdf_trailer{};
				current_trailer.offset = i;
				in_trailer = true;
			}
		}

		if (match_str(i, "startxref")) {
			if (in_xref) {
				current_xref.size = i - current_xref.offset;
				xrefs_.push_back(current_xref);
				in_xref = false;
			}
			if (in_trailer) {
				current_trailer.size = i - current_trailer.offset;
				trailers_.push_back(current_trailer);
				in_trailer = false;
			}
			pdf_trailer st;
			st.offset = i;
			size_t eof_pos = i;
			while (eof_pos < data.size() && !match_str(eof_pos, "%%EOF")) {
				eof_pos++;
			}
			if (eof_pos < data.size()) {
				st.size = (eof_pos + 5) - i;
			} else {
				st.size = data.size() - i;
			}
			trailers_.push_back(st);
			i = eof_pos;
		}

		i++;
	}

	if (in_obj) {
		current_obj.size = data.size() - current_obj.offset;
		objects_.push_back(current_obj);
	}
	if (in_xref) {
		current_xref.size = data.size() - current_xref.offset;
		xrefs_.push_back(current_xref);
	}
	if (in_trailer) {
		current_trailer.size = data.size() - current_trailer.offset;
		trailers_.push_back(current_trailer);
	}

	parsed_successfully_ = true;
	return true;
}

highlight_info pdf_hex_highlighter::get_info(const std::vector<uint8_t> &data, size_t offset) const
{
	(void)data;
	if (!parsed_successfully_) {
		return {hex_semantic_type::normal, "", 0, 0};
	}

	if (offset < header_size_) {
		return {hex_semantic_type::file_header, "PDF Header", 0, header_size_};
	}

	for (const auto &obj : objects_) {
		if (obj.stream_size > 0 && offset >= obj.stream_offset && offset < obj.stream_offset + obj.stream_size) {
			return {hex_semantic_type::data_section, "PDF Object Stream", obj.stream_offset, obj.stream_size};
		}
		if (offset >= obj.offset && offset < obj.offset + obj.size) {
			return {hex_semantic_type::prog_header, "PDF Object", obj.offset, obj.size};
		}
	}

	for (const auto &xr : xrefs_) {
		if (offset >= xr.offset && offset < xr.offset + xr.size) {
			return {hex_semantic_type::sect_header, "PDF Cross-Reference Table (xref)", xr.offset, xr.size};
		}
	}

	for (const auto &tr : trailers_) {
		if (offset >= tr.offset && offset < tr.offset + tr.size) {
			return {hex_semantic_type::magic, "PDF Trailer / startxref", tr.offset, tr.size};
		}
	}

	return {hex_semantic_type::normal, "", 0, 0};
}

size_t pdf_hex_highlighter::get_next_symbol_offset(size_t current_offset) const
{
	std::vector<size_t> offsets;
	offsets.push_back(header_size_);

	for (const auto &obj : objects_) {
		offsets.push_back(obj.offset);
		if (obj.stream_size > 0) {
			offsets.push_back(obj.stream_offset);
			offsets.push_back(obj.stream_offset + obj.stream_size);
		}
		offsets.push_back(obj.offset + obj.size);
	}
	for (const auto &xr : xrefs_) {
		offsets.push_back(xr.offset);
		offsets.push_back(xr.offset + xr.size);
	}
	for (const auto &tr : trailers_) {
		offsets.push_back(tr.offset);
		offsets.push_back(tr.offset + tr.size);
	}

	std::sort(offsets.begin(), offsets.end());
	for (size_t off : offsets) {
		if (off > current_offset) {
			return off;
		}
	}
	return current_offset;
}

std::optional<size_t> pdf_hex_highlighter::get_offset_by_name(const std::string &name) const
{
	if (!parsed_successfully_) {
		return std::nullopt;
	}
	for (const auto &obj : objects_) {
		if (obj.id == name || obj.id + " obj" == name) {
			return obj.offset;
		}
	}
	return std::nullopt;
}

std::string pdf_hex_highlighter::get_structure_summary() const
{
	if (!parsed_successfully_) {
		return "";
	}

	std::string summary = "### PDF Structural Overview\n\n";

	// Add Metadata Table
	summary += "#### File Metadata\n\n";
	summary += "| Property | Value |\n";
	summary += "| --- | --- |\n";
	summary += std::format("| **MIME Type** | {} |\n", mime_type_);
	summary += std::format("| **Description** | {} |\n\n", description_);

	if (!objects_.empty()) {
		summary += "#### PDF Objects\n\n";
		summary += "| Object ID | Offset | Size (Bytes) | Stream Offset | Stream Size (Bytes) |\n";
		summary += "| :---: | :---: | :---: | :---: | :---: |\n";
		for (const auto &obj : objects_) {
			summary += std::format("| `{}` | `0x{:X}` | `{}` | `0x{:X}` | `{}` |\n",
			                       obj.id, obj.offset, obj.size, obj.stream_offset, obj.stream_size);
		}
		summary += "\n";
	}

	if (!xrefs_.empty()) {
		summary += "#### Cross-Reference (XRef) Tables\n\n";
		summary += "| Index | Offset | Size (Bytes) |\n";
		summary += "| :---: | :---: | :---: |\n";
		for (size_t i = 0; i < xrefs_.size(); ++i) {
			summary += std::format("| `{}` | `0x{:X}` | `{}` |\n", i + 1, xrefs_[i].offset, xrefs_[i].size);
		}
		summary += "\n";
	}

	if (!trailers_.empty()) {
		summary += "#### Trailers\n\n";
		summary += "| Index | Offset | Size (Bytes) |\n";
		summary += "| :---: | :---: | :---: |\n";
		for (size_t i = 0; i < trailers_.size(); ++i) {
			summary += std::format("| `{}` | `0x{:X}` | `{}` |\n", i + 1, trailers_[i].offset, trailers_[i].size);
		}
		summary += "\n";
	}

	return summary;
}
