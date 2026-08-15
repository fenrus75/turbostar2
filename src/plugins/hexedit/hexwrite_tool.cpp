#include "plugins/hexedit/hexwrite_tool.h"
#include <cctype>
#include <filesystem>
#include <format>
#include <fstream>
#include <vector>
#include "hex/hex_highlighter_registry.h"

namespace tools
{

namespace
{
std::vector<uint8_t> parse_hex_string(const std::string &input, std::string &out_error)
{
	std::string hex_chars;
	size_t idx = 0;
	while (idx < input.length()) {
		char c = input[idx];
		if (std::isspace(c) || c == ',' || c == ':' || c == '-' || c == ';' || c == '[' || c == ']' || c == '{' || c == '}') {
			idx++;
			continue;
		}
		if (idx + 1 < input.length() && input[idx] == '0' && (input[idx + 1] == 'x' || input[idx + 1] == 'X')) {
			idx += 2;
			continue;
		}
		if (std::isxdigit(c)) {
			hex_chars.push_back(c);
		} else {
			out_error = std::format("Invalid character '{}' in hex input.", c);
			return {};
		}
		idx++;
	}

	if (hex_chars.length() % 2 != 0) {
		out_error = "Hex data string must have an even number of hex digits.";
		return {};
	}

	std::vector<uint8_t> bytes;
	bytes.reserve(hex_chars.length() / 2);
	for (size_t i = 0; i < hex_chars.length(); i += 2) {
		std::string byte_str = hex_chars.substr(i, 2);
		uint8_t byte = static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16));
		bytes.push_back(byte);
	}

	return bytes;
}
} // namespace

hexwrite_tool::hexwrite_tool(hexwrite_args args) : llm_tool_action("Hexwriting file"), args_(std::move(args))
{
}

bool hexwrite_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string hexwrite_tool::execute(agentlib::tool_context &ctx)
{
	constexpr size_t kMaxHexWriteOffset = 512 * 1024 * 1024; // 512 MB cap
	constexpr size_t kMaxHexWriteData   = 64  * 1024 * 1024; // 64 MB cap

	if (args_.safe_path.find("file://") == 0) {
		args_.safe_path = args_.safe_path.substr(7);
	}

	auto vfs = ctx.fs_security.get_vfs();
	if (vfs && vfs->is_local_path_available(args_.safe_path)) {
		args_.safe_path = vfs->get_local_path(args_.safe_path);
	}

	std::string error;
	std::vector<uint8_t> bytes = parse_hex_string(args_.hex_data, error);
	if (!error.empty()) {
		set_failure(ctx, error);
		return "Error: " + error;
	}

	if (bytes.empty()) {
		set_failure(ctx, "No data provided to write.");
		return "Error: No data provided to write.";
	}

	if (bytes.size() > kMaxHexWriteData) {
		set_failure(ctx, "Hex data payload exceeds maximum allowed limit (64 MB).");
		return "Error: Hex data payload exceeds maximum allowed limit (64 MB).";
	}

	size_t start = args_.offset;
	if (start > kMaxHexWriteOffset) {
		set_failure(ctx, "Write offset exceeds maximum allowed limit (512 MB).");
		return "Error: Write offset exceeds maximum allowed limit (512 MB).";
	}

	if (!args_.offset_by_name.empty()) {
		std::ifstream infile(args_.safe_path, std::ios::binary);
		if (!infile.is_open()) {
			set_failure(ctx, "Could not open file to resolve offset by name: " + args_.safe_path);
			return "Error: Could not open file to resolve offset by name: " + args_.safe_path;
		}

		infile.seekg(0, std::ios::end);
		std::streamsize file_size = infile.tellg();
		infile.seekg(0, std::ios::beg);

		if (file_size > 50 * 1024 * 1024) {
			infile.close();
			set_failure(ctx, "File size exceeds 50MB limit for offset_by_name resolution.");
			return "Error: File size exceeds 50MB limit for offset_by_name resolution.";
		}

		std::vector<uint8_t> file_bytes(file_size);
		if (file_size > 0 && infile.read(reinterpret_cast<char *>(file_bytes.data()), file_size)) {
			auto highlighter = hex_highlighter_registry::get_instance().detect_highlighter(file_bytes);
			if (highlighter && highlighter->parse(file_bytes)) {
				auto resolved_offset = highlighter->get_offset_by_name(args_.offset_by_name);
				if (resolved_offset) {
					start = *resolved_offset;
				} else {
					infile.close();
					set_failure(ctx, "Could not resolve named chunk or symbol: " + args_.offset_by_name);
					return "Error: Could not resolve named chunk or symbol: " + args_.offset_by_name;
				}
			} else {
				infile.close();
				set_failure(ctx, "File format not supported for offset resolution by name.");
				return "Error: Cannot resolve offset by name for this file format (no highlighter).";
			}
		} else if (file_size == 0) {
			infile.close();
			set_failure(ctx, "File is empty; cannot resolve offset by name.");
			return "Error: File is empty; cannot resolve offset by name.";
		}
		infile.close();
	}

	if (start > kMaxHexWriteOffset) {
		set_failure(ctx, "Resolved write offset exceeds maximum allowed limit (512 MB).");
		return "Error: Resolved write offset exceeds maximum allowed limit (512 MB).";
	}

	// Create file if it does not exist
	if (!std::filesystem::exists(args_.safe_path)) {
		// Make sure parent directories exist
		std::filesystem::path parent = std::filesystem::path(args_.safe_path).parent_path();
		if (!parent.empty() && !std::filesystem::exists(parent)) {
			std::filesystem::create_directories(parent);
		}
		std::ofstream out(args_.safe_path, std::ios::binary);
		if (!out) {
			set_failure(ctx, "Failed to create new file.");
			return "Error: Failed to create new file.";
		}
		out.close();
	}

	// Open for reading and writing in binary mode
	std::fstream file(args_.safe_path, std::ios::in | std::ios::out | std::ios::binary);
	if (!file.is_open()) {
		set_failure(ctx, "Failed to open file for writing.");
		return "Error: Failed to open file for writing.";
	}

	// Get current file size
	file.seekg(0, std::ios::end);
	size_t current_size = file.tellg();

	// Pad with zero bytes if offset is beyond current file size
	if (start > current_size) {
		file.seekp(current_size);
		size_t pad_size = start - current_size;
		std::vector<char> pad(pad_size, 0);
		file.write(pad.data(), pad_size);
	}

	// Write the hex data at target offset (overwrite mode)
	file.seekp(start);
	file.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
	file.close();

	std::string success_msg = std::format("Successfully wrote {} bytes to {} at offset {}.", bytes.size(), args_.requested_path, start);
	set_success(ctx, success_msg);
	return success_msg;
}

} // namespace tools
