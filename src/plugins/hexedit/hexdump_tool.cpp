#include "plugins/hexedit/hexdump_tool.h"
#include "hex/hex_highlighter.h"
#include "hex/hex_highlighter_registry.h"
#include "mapped_file.h"
#include <algorithm>
#include <filesystem>
#include <format>
#include <span>
#include <vector>

namespace tools
{

namespace
{
struct referenced_string_literal {
	size_t id{0};
	std::string address;
	std::string value;
};

std::string extract_hex_address_token(std::string_view inst_part)
{
	size_t hex_pos = inst_part.find("0x");
	if (hex_pos == std::string_view::npos) {
		return "";
	}
	size_t end_pos = hex_pos + 2;
	while (end_pos < inst_part.size() &&
	       ((inst_part[end_pos] >= '0' && inst_part[end_pos] <= '9') ||
	        (inst_part[end_pos] >= 'a' && inst_part[end_pos] <= 'f') ||
	        (inst_part[end_pos] >= 'A' && inst_part[end_pos] <= 'F'))) {
		++end_pos;
	}
	return std::string(inst_part.substr(hex_pos, end_pos - hex_pos));
}

std::string compact_code_annotation(std::string_view desc, std::vector<referenced_string_literal> &string_literals)
{
	constexpr std::string_view marker = " (string: \"";
	size_t marker_pos = desc.find(marker);
	if (marker_pos == std::string_view::npos) {
		return std::string(desc);
	}
	size_t val_start = marker_pos + marker.size();
	size_t val_end = desc.rfind("\")");
	if (val_end != std::string_view::npos && val_end >= val_start) {
		std::string_view inst_part = desc.substr(0, marker_pos);
		std::string_view str_val = desc.substr(val_start, val_end - val_start);
		std::string addr = extract_hex_address_token(inst_part);

		size_t assigned_id = 0;
		for (const auto &entry : string_literals) {
			if ((!addr.empty() && entry.address == addr) || (addr.empty() && entry.value == str_val)) {
				assigned_id = entry.id;
				break;
			}
		}
		if (assigned_id == 0 && string_literals.size() < 256) {
			assigned_id = string_literals.size() + 1;
			string_literals.push_back(referenced_string_literal{
			    .id = assigned_id, .address = std::move(addr), .value = std::string(str_val)});
		}
		if (assigned_id > 0) {
			return std::format("{} <str#{}>", inst_part, assigned_id);
		}
	}
	return std::string(desc);
}

std::string format_hexdump_row(std::span<const uint8_t> data,
                               size_t row_start,
                               size_t read_start,
                               size_t read_limit,
                               const hex_highlighter *highlighter,
                               std::vector<referenced_string_literal> &string_literals)
{
	std::string hex_part;
	std::string ascii_part;
	std::vector<std::string> row_annotations;
	size_t last_code_range_start = SIZE_MAX;
	size_t effective_row_start = std::max(row_start, read_start);

	for (size_t col = 0; col < 16; ++col) {
		size_t offset = row_start + col;
		if (offset < read_limit) {
			uint8_t byte = data[offset];
			hex_part += std::format("{:02x}", byte);
			if (col == 7) {
				hex_part += "  ";
			} else if (col < 15) {
				hex_part += " ";
			}
			if (byte >= 32 && byte <= 126) {
				ascii_part.push_back(static_cast<char>(byte));
			} else {
				ascii_part.push_back('.');
			}

			if (highlighter && offset >= read_start) {
				highlight_info info = highlighter->get_info(data, offset);
				if (info.type != hex_semantic_type::normal && !info.description.empty()) {
					if (info.type == hex_semantic_type::code_section && info.range_size > 0 &&
					    info.description.find(" | ELF Sec ") != std::string::npos) {
						if (info.range_start >= effective_row_start && info.range_start != last_code_range_start) {
							last_code_range_start = info.range_start;
							row_annotations.push_back(compact_code_annotation(info.description, string_literals));
						}
					} else if (row_annotations.empty() || row_annotations.back() != info.description) {
						row_annotations.push_back(info.description);
					}
				}
			}
		} else {
			// Padding
			hex_part += "  ";
			if (col == 7) {
				hex_part += "  ";
			} else if (col < 15) {
				hex_part += " ";
			}
			ascii_part.push_back(' ');
		}
	}

	std::string annotation_suffix;
	if (!row_annotations.empty()) {
		annotation_suffix = "  # ";
		for (size_t a = 0; a < row_annotations.size(); ++a) {
			if (a > 0) annotation_suffix += " | ";
			annotation_suffix += row_annotations[a];
		}
	}

	return std::format("{:08x}: {}  |{}|{}", row_start, hex_part, ascii_part, annotation_suffix);
}
} // namespace

hexdump_tool::hexdump_tool(hexdump_args args) : llm_tool_action("Hexdumping file"), args_(std::move(args))
{
}

bool hexdump_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string hexdump_tool::execute(agentlib::tool_context &ctx)
{
	auto vfs = ctx.fs_security.get_vfs();
	if (vfs && vfs->is_local_path_available(args_.safe_path)) {
		args_.safe_path = vfs->get_local_path(args_.safe_path);
	}

	if (!std::filesystem::exists(args_.safe_path)) {
		set_failure(ctx, "File does not exist.");
		return "Error: File does not exist.";
	}

	mapped_file file_data;
	std::string map_err;
	if (!file_data.open(args_.safe_path, map_err)) {
		set_failure(ctx, "Failed to open file for reading: " + map_err);
		return "Error: Failed to open file for reading: " + map_err;
	}
	// Detect and parse using registry
	auto &registry = hex_highlighter_registry::get_instance();
	std::shared_ptr<hex_highlighter> highlighter = registry.detect_highlighter(file_data);
	if (highlighter) {
		highlighter->parse(file_data);
	}

	size_t start = args_.offset;
	size_t effective_size = args_.size;
	if (!args_.offset_by_name.empty()) {
		if (!highlighter) {
			set_failure(ctx, "File format not supported for offset resolution by name.");
			return "Error: Cannot resolve offset by name for this file format (no highlighter).";
		}
		auto resolved_offset = highlighter->get_offset_by_name(args_.offset_by_name);
		if (!resolved_offset) {
			set_failure(ctx, "Could not find offset for name: " + args_.offset_by_name);
			return "Error: Could not resolve named chunk or symbol: " + args_.offset_by_name;
		}
		start = *resolved_offset;
		if (effective_size == 0) {
			if (auto resolved_size = highlighter->get_size_by_name(args_.offset_by_name); resolved_size && *resolved_size > 0) {
				effective_size = std::min<size_t>(*resolved_size, 4096);
			}
		}
	}
	if (effective_size == 0) {
		effective_size = 256;
	}

	if (start >= file_data.size()) {
		set_failure(ctx, "start_offset is out of bounds.");
		return "Error: start_offset is out of bounds.";
	}

	size_t limit = (effective_size > file_data.size() - start) ? file_data.size() : start + effective_size;
	std::vector<referenced_string_literal> string_literals;
	std::string result = std::format("### Hexdump: {} [0x{:X} - 0x{:X}]\n```\n", args_.requested_path, start, limit);
	for (size_t row = (start / 16) * 16; row < limit; row += 16) {
		result += format_hexdump_row(file_data, row, start, limit, highlighter.get(), string_literals) + "\n";
	}
	result += "```\n";
	if (!string_literals.empty()) {
		result += "\n#### Referenced String Literals\n";
		result += "| ID | Address | Value |\n";
		result += "| --- | --- | --- |\n";
		for (const auto &entry : string_literals) {
			std::string escaped_val;
			escaped_val.reserve(entry.value.size());
			for (char c : entry.value) {
				if (c == '|') {
					escaped_val += "\\|";
				} else {
					escaped_val.push_back(c);
				}
			}
			result += std::format("| `str#{}` | `{}` | `\"{}\"` |\n",
			                      entry.id,
			                      entry.address.empty() ? "-" : entry.address,
			                      escaped_val);
		}
	}
	set_success(ctx, std::format("Read {} bytes.", limit - start));
	return result;
}

} // namespace tools
