#include "plugins/hexedit/hexdump_tool.h"
#include "hex/hex_highlighter.h"
#include "hex/hex_highlighter_registry.h"
#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <vector>

namespace tools
{

namespace
{
std::string format_hexdump_row(const std::vector<uint8_t> &data, size_t row_start, size_t read_limit, const hex_highlighter *highlighter)
{
	std::string hex_part;
	std::string ascii_part;
	std::vector<std::string> row_annotations;

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

			if (highlighter) {
				highlight_info info = highlighter->get_info(data, offset);
				if (info.type != hex_semantic_type::normal && !info.description.empty()) {
					if (row_annotations.empty() || row_annotations.back() != info.description) {
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

	std::ifstream file(args_.safe_path, std::ios::binary);
	if (!file.is_open()) {
		set_failure(ctx, "Failed to open file for reading.");
		return "Error: Failed to open file for reading.";
	}

	// Read full file bytes first to run structural highlighter properly
	file.seekg(0, std::ios::end);
	std::streamsize file_size = file.tellg();
	file.seekg(0, std::ios::beg);

	if (file_size > 50 * 1024 * 1024) {
		set_failure(ctx, "File is too large (>50MB).");
		return "Error: File is too large (>50MB) to read.";
	}

	std::vector<uint8_t> file_data(file_size);
	if (file_size > 0 && !file.read(reinterpret_cast<char *>(file_data.data()), file_size)) {
		file.close();
		set_failure(ctx, "Failed to read file contents.");
		return "Error: Failed to read file contents.";
	}
	file.close();

	// Detect and parse using registry
	auto &registry = hex_highlighter_registry::get_instance();
	std::shared_ptr<hex_highlighter> highlighter = registry.detect_highlighter(file_data);
	if (highlighter) {
		highlighter->parse(file_data);
	}

	size_t start = args_.offset;
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
	}

	if (start >= file_data.size()) {
		set_failure(ctx, "start_offset is out of bounds.");
		return "Error: start_offset is out of bounds.";
	}

	size_t limit = (args_.size > file_data.size() - start) ? file_data.size() : start + args_.size;

	std::string result = std::format("### Hexdump: {} [0x{:X} - 0x{:X}]\n```\n", args_.requested_path, start, limit);
	for (size_t row = (start / 16) * 16; row < limit; row += 16) {
		result += format_hexdump_row(file_data, row, limit, highlighter.get()) + "\n";
	}
	result += "```\n";

	set_success(ctx, std::format("Read {} bytes.", limit - start));
	return result;
}

} // namespace tools
