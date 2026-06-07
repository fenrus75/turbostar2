#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <vector>
#include "../../ui/hex_highlighter.h"
#include "hex_inspect_range.h"

namespace tools
{

hex_inspect_range_tool::hex_inspect_range_tool(hex_inspect_range_args args)
    : llm_tool_action("Inspecting binary structures in " + args.requested_path), args_(std::move(args))
{
}

bool hex_inspect_range_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string hex_inspect_range_tool::execute(agentlib::tool_context &ctx)
{
	std::ifstream file(args_.safe_path, std::ios::binary);
	if (!file.is_open()) {
		set_failure(ctx, "Failed to open file.");
		return "Error: Failed to open file for reading.";
	}

	file.seekg(0, std::ios::end);
	std::streamsize size = file.tellg();
	file.seekg(0, std::ios::beg);

	std::vector<uint8_t> bytes(size);
	if (size > 0 && !file.read(reinterpret_cast<char *>(bytes.data()), size)) {
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

	std::string result = std::format("### Binary Structure Inspection: {} [0x{:X} - 0x{:X}]\n\n", args_.requested_path, start, end);

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
