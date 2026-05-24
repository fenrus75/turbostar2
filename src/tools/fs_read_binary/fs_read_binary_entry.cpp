#include <filesystem>
#include <fstream>
#include <span>
#include <sstream>
#include <sys/stat.h>
#include <vector>
#include "fs_read_binary.h"

namespace tools
{

static std::string base64_encode(std::span<const unsigned char> buf)
{
	static const char trailing_char = '=';
	static const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
					   "abcdefghijklmnopqrstuvwxyz"
					   "0123456789+/";

	std::string ret;
	int i = 0;
	int j = 0;
	unsigned char char_array_3[3];
	unsigned char char_array_4[4];

	for (unsigned char byte : buf) {
		char_array_3[i++] = byte;
		if (i == 3) {
			char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
			char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
			char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
			char_array_4[3] = char_array_3[2] & 0x3f;

			for (i = 0; (i < 4); i++)
				ret += base64_chars[char_array_4[i]];
			i = 0;
		}
	}

	if (i) {
		for (j = i; j < 3; j++)
			char_array_3[j] = '\0';

		char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
		char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
		char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
		char_array_4[3] = char_array_3[2] & 0x3f;

		for (j = 0; (j < i + 1); j++)
			ret += base64_chars[char_array_4[j]];

		while ((i++ < 3))
			ret += trailing_char;
	}

	return ret;
}

fs_read_binary_tool::fs_read_binary_tool(fs_read_binary_args args) : args_(std::move(args))
{
}

bool fs_read_binary_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string fs_read_binary_tool::execute(agentlib::tool_context &ctx)
{
	if (args_.safe_path.starts_with("skills://")) {
		auto vfs = ctx.fs_security.get_vfs();
		if (vfs) {
			auto view_opt = vfs->read_file(args_.safe_path);
			if (view_opt) {
				std::string_view view = view_opt.value();
				size_t start = args_.start_offset;
				if (start >= view.length()) {
					return "Error: start_offset is out of bounds.";
				}
				size_t len = view.length() - start;
				if (args_.size >= 0 && static_cast<size_t>(args_.size) < len) {
					len = args_.size;
				}
				if (len > 50 * 1024 * 1024) {
					len = 50 * 1024 * 1024;
				}
				if (len == 0) {
					return "";
				}
				return base64_encode(
				    std::span<const unsigned char>(reinterpret_cast<const unsigned char *>(view.data()) + start, len));
			}
		}
		return "Error: Virtual file not found or not mounted.";
	}

	struct stat sb;
	if (stat(args_.safe_path.c_str(), &sb) == -1) {
		return "Error: File does not exist or cannot be accessed.";
	}

	// Default to 50MB max like fs_read_lines
	if (sb.st_size > 50 * 1024 * 1024) {
		return "Error: File is too large (>50MB) to read directly.";
	}

	std::ifstream file(args_.safe_path, std::ios::binary);
	if (!file.is_open()) {
		return "Error: Could not open file for reading.";
	}

	if (args_.start_offset > 0) {
		file.seekg(args_.start_offset);
		if (file.fail()) {
			return "Error: start_offset is out of bounds.";
		}
	}

	// Determine how much to read
	std::streamsize bytes_to_read = sb.st_size - args_.start_offset;
	if (bytes_to_read < 0) {
		return "Error: start_offset is out of bounds.";
	}

	if (args_.size >= 0 && args_.size < bytes_to_read) {
		bytes_to_read = args_.size;
	}

	// Hard limit on bytes to read (e.g. 50MB)
	if (bytes_to_read > 50 * 1024 * 1024) {
		bytes_to_read = 50 * 1024 * 1024;
	}

	if (bytes_to_read == 0) {
		return "";
	}

	std::vector<unsigned char> buffer(bytes_to_read);
	file.read(reinterpret_cast<char *>(buffer.data()), bytes_to_read);
	std::streamsize bytes_read = file.gcount();

	if (bytes_read == 0) {
		return "Requested range is empty or past the end of the file.";
	}

	return base64_encode(std::span<const unsigned char>(buffer.data(), bytes_read));
}

} // namespace tools
