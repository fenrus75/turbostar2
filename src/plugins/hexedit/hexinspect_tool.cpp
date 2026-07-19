#include "plugins/hexedit/hexinspect_tool.h"
#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <vector>
#include "hex/hex_highlighter.h"
#include "hex/hex_highlighter_registry.h"
#include "mime.h"

namespace tools
{

namespace
{
// Helper to retrieve the file's MIME type using libmagic if available.
std::string get_file_mime_type(const std::string &path)
{
	return mime::detect_file_type(path);
}

// Helper to retrieve the detailed file description using libmagic if available.
std::string get_file_description(const std::string &path)
{
	std::string desc = mime::detect_file_description(path);
	if (desc == "Unknown file type") {
		return "unknown";
	}
	return desc;
}

// Sanitizes text values for safe inclusion in a Markdown table (escaping pipe characters).
std::string sanitize_for_table(std::string str)
{
	std::replace(str.begin(), str.end(), '|', ',');
	return str;
}
} // namespace

hexinspect_tool::hexinspect_tool(hexinspect_args args)
    : llm_tool_action("Inspecting binary structures in " + args.requested_path), args_(std::move(args))
{
}

bool hexinspect_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string hexinspect_tool::execute(agentlib::tool_context &ctx)
{
	auto vfs = ctx.fs_security.get_vfs();
	if (vfs && vfs->is_local_path_available(args_.safe_path)) {
		args_.safe_path = vfs->get_local_path(args_.safe_path);
	}

	std::ifstream file(args_.safe_path, std::ios::binary);
	if (!file.is_open()) {
		set_failure(ctx, "Failed to open file.");
		return "Error: Failed to open file for reading.";
	}

	file.seekg(0, std::ios::end);
	std::streamsize file_size = file.tellg();
	file.seekg(0, std::ios::beg);

	if (file_size > 50 * 1024 * 1024) {
		file.close();
		set_failure(ctx, "File is too large (>50MB).");
		return "Error: File is too large (>50MB) to read.";
	}

	std::vector<uint8_t> bytes(file_size);
	if (file_size > 0 && !file.read(reinterpret_cast<char *>(bytes.data()), file_size)) {
		file.close();
		set_failure(ctx, "Failed to read file.");
		return "Error: Failed to read file.";
	}
	file.close();

	if (bytes.empty()) {
		set_success(ctx, "Empty file.");
		return "File is empty.";
	}

	auto highlighter = hex_highlighter_registry::get_instance().detect_highlighter(bytes);
	if (!highlighter) {
		set_failure(ctx, "No parser/highlighter registered for this binary format.");
		return "Error: No structural parser registered for this file format.";
	}

	if (!highlighter->parse(bytes)) {
		set_failure(ctx, "Failed to parse binary file structure.");
		return "Error: Failed to parse structural format of the file.";
	}

	size_t start = args_.start_offset;
	if (!args_.offset_by_name.empty()) {
		auto resolved_offset = highlighter->get_offset_by_name(args_.offset_by_name);
		if (!resolved_offset) {
			set_failure(ctx, "Could not find offset for name: " + args_.offset_by_name);
			return "Error: Could not resolve named chunk or symbol: " + args_.offset_by_name;
		}
		start = *resolved_offset;
	}

	if (start >= bytes.size()) {
		set_failure(ctx, "start_offset is out of bounds.");
		return "Error: start_offset is out of bounds.";
	}

	size_t end = start + args_.size;
	if (end > bytes.size()) {
		end = bytes.size();
	}

	// Retrieve general file details and MIME type via libmagic (or fallbacks)
	std::string mime_type = get_file_mime_type(args_.safe_path);
	std::string description = get_file_description(args_.safe_path);

	// Construct header and metadata summary table
	std::string result = std::format("### Binary Structure Inspection: {} [0x{:X} - 0x{:X}]\n\n", args_.requested_path, start, end);
	result += "| Property | Value |\n";
	result += "| --- | --- |\n";
	result += std::format("| **MIME Type** | {} |\n", sanitize_for_table(mime_type));
	result += std::format("| **Description** | {} |\n\n", sanitize_for_table(description));

	if (args_.start_offset == 0 && args_.offset_by_name.empty()) {
		std::string structure_summary = highlighter->get_structure_summary();
		if (!structure_summary.empty()) {
			size_t line_count = 0;
			for (char c : structure_summary) {
				if (c == '\n') {
					line_count++;
				}
			}

			// If the table is relatively small, include it directly.
			// Otherwise, write it to a tmp:// file and include a preview.
			if (line_count <= 40) {
				result += structure_summary;
			} else {
				std::string filename = std::filesystem::path(args_.requested_path).filename().string();
				std::string tmp_uri = "tmp://archive_contents_" + filename + ".md";

				if (vfs) {
					vfs->write_file(tmp_uri, structure_summary.data(), structure_summary.size());
				}

				// Preview the first 15 lines of the table (header + first few entries)
				std::string preview;
				size_t current_line = 0;
				size_t pos = 0;
				while (current_line < 15 && pos < structure_summary.size()) {
					size_t next_nl = structure_summary.find('\n', pos);
					if (next_nl == std::string::npos) {
						preview += structure_summary.substr(pos);
						break;
					}
					preview += structure_summary.substr(pos, next_nl - pos + 1);
					pos = next_nl + 1;
					current_line++;
				}

				result += preview;
				result += std::format("\n*Archive content summary was too large for direct inclusion ({} lines), but the complete listing has been written to {}*\n\n", line_count, tmp_uri);
			}
		}
	}

	result += "### Structure Details\n";

	size_t offset = start;
	while (offset < end) {
		highlight_info info = highlighter->get_info(bytes, offset);

		size_t length = info.range_size;
		if (length == 0) {
			length = 1;
		}

		if (info.type != hex_semantic_type::normal && !info.description.empty()) {
			std::string hex_val;
			size_t read_len = std::min(length, end - offset);
			for (size_t i = 0; i < read_len; ++i) {
				if (i > 0) {
					hex_val += " ";
				}
				hex_val += std::format("{:02x}", bytes[offset + i]);
			}
			result +=
			    std::format("* **[0x{:X} - 0x{:X}]**: `{}` | {}\n", offset, offset + length - 1, hex_val, info.description);
		}

		offset += length;
	}

	set_success(ctx, "Inspected " + std::to_string(end - start) + " bytes.");
	return result;
}

} // namespace tools
