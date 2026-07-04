#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <span>
#include <sstream>
#include <sys/stat.h>
#include <vector>
#include "fs_read_binary.h"
#include "fs_utils.h"
#include "tools/magic_compat.h"

namespace tools
{

static std::string get_mime_by_extension(const std::string &path)
{
	size_t dot = path.find_last_of('.');
	if (dot == std::string::npos) {
		return "application/octet-stream";
	}
	std::string ext = path.substr(dot + 1);
	std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

	if (ext == "png") return "image/png";
	if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
	if (ext == "gif") return "image/gif";
	if (ext == "webp") return "image/webp";
	if (ext == "pdf") return "application/pdf";
	if (ext == "txt") return "text/plain";
	if (ext == "html" || ext == "htm") return "text/html";
	if (ext == "css") return "text/css";
	if (ext == "js") return "text/javascript";
	if (ext == "json") return "application/json";
	if (ext == "xml") return "application/xml";
	if (ext == "zip") return "application/zip";
	if (ext == "tar") return "application/x-tar";
	if (ext == "gz") return "application/gzip";
	if (ext == "mp3") return "audio/mpeg";
	if (ext == "mp4") return "video/mp4";
	if (ext == "wav") return "audio/wav";
	if (ext == "ogg") return "audio/ogg";
	return "application/octet-stream";
}

static std::string detect_mime_type(const std::string &path)
{
#ifdef HAS_LIBMAGIC
	if (path.find("://") == std::string::npos) {
		magic_t magic = magic_open(MAGIC_MIME_TYPE);
		if (magic) {
			if (magic_load(magic, nullptr) == 0) {
				const char *mime = magic_file(magic, path.c_str());
				if (mime) {
					std::string res(mime);
					magic_close(magic);
					return res;
				}
			}
			magic_close(magic);
		}
	}
#endif
	return get_mime_by_extension(path);
}

static std::string format_binary_output(std::span<const unsigned char> data, const std::string &format, const std::string &mime_type)
{
	if (format == "hex") {
		std::string hex_str;
		for (size_t i = 0; i < data.size(); ++i) {
			if (i > 0) {
				hex_str += " ";
			}
			hex_str += std::format("{:02x}", data[i]);
		}
		return hex_str;
	}
	return std::format("data:{};base64,{}", mime_type, fs_utils::base64_encode(data));
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
	if (args_.safe_path.find("://") != std::string::npos) {
		auto vfs = ctx.fs_security.get_vfs();
		if (vfs) {
			auto view_opt = vfs->read_file(args_.safe_path);
			if (view_opt) {
				std::string_view view = view_opt.value()->view();
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
				return format_binary_output(
				    std::span<const unsigned char>(reinterpret_cast<const unsigned char *>(view.data()) + start, len),
				    args_.format,
				    detect_mime_type(args_.safe_path));
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

	return format_binary_output(std::span<const unsigned char>(buffer.data(), bytes_read), args_.format, detect_mime_type(args_.safe_path));
}

} // namespace tools
