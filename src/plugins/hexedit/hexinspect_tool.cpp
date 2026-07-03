#include "plugins/hexedit/hexinspect_tool.h"
#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <vector>
#include "hex/hex_highlighter.h"
#include "hex/hex_highlighter_registry.h"
#include "tools/magic_compat.h"

namespace tools
{

namespace
{
// Helper to retrieve the file's MIME type using libmagic if available.
std::string get_file_mime_type([[maybe_unused]] const std::string &path)
{
#ifdef HAS_LIBMAGIC
	magic_t magic = magic_open(MAGIC_MIME_TYPE);
	if (!magic) {
		return "unknown";
	}
	if (magic_load(magic, nullptr) != 0) {
		magic_close(magic);
		return "unknown";
	}
	const char *mime = magic_file(magic, path.c_str());
	std::string res = mime ? mime : "unknown";
	magic_close(magic);
	return res;
#else
	return "unknown (libmagic disabled)";
#endif
}

// Helper to retrieve the detailed file description using libmagic if available.
std::string get_file_description([[maybe_unused]] const std::string &path)
{
#ifdef HAS_LIBMAGIC
	magic_t magic = magic_open(MAGIC_NONE);
	if (!magic) {
		return "unknown";
	}
	if (magic_load(magic, nullptr) != 0) {
		magic_close(magic);
		return "unknown";
	}
	const char *desc = magic_file(magic, path.c_str());
	std::string res = desc ? desc : "unknown";
	magic_close(magic);
	return res;
#else
	return "unknown (libmagic disabled)";
#endif
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
