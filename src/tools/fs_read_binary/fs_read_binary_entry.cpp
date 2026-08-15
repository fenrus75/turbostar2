#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <span>
#include <sys/stat.h>
#include "fs_read_binary.h"
#include "fs_utils.h"
#include "mime.h"

namespace tools
{

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
				size_t start = args_.offset;
				if (start >= view.length()) {
					return fs_utils::wrap_prompt_untrusted_data_tag("fs_read_binary_result", "Error: offset is out of bounds.");
				}
				size_t len = view.length() - start;
				if (args_.size >= 0 && static_cast<size_t>(args_.size) < len) {
					len = args_.size;
				}
				if (len > 50 * 1024 * 1024) {
					len = 50 * 1024 * 1024;
				}
				if (len == 0) {
					return fs_utils::wrap_prompt_untrusted_data_tag("fs_read_binary_result", "");
				}
				std::string res = fs_utils::format_binary_output(
				    std::span<const unsigned char>(reinterpret_cast<const unsigned char *>(view.data()) + start, len),
				    args_.format,
				    mime::detect_file_type(args_.safe_path));
				return fs_utils::wrap_prompt_untrusted_data_tag("fs_read_binary_result", res);
			}
		}
		return fs_utils::wrap_prompt_untrusted_data_tag("fs_read_binary_result", "Error: Virtual file not found or not mounted.");
	}

	std::string resolved_path;
	std::string err;
	if (!ctx.fs_security.validate_access(args_.safe_path, agentlib::access_type::read, resolved_path, err)) {
		return fs_utils::wrap_prompt_untrusted_data_tag("fs_read_binary_result", "Error: Access denied: " + err);
	}

	struct stat sb;
	if (stat(resolved_path.c_str(), &sb) == -1) {
		return fs_utils::wrap_prompt_untrusted_data_tag("fs_read_binary_result", "Error: File does not exist or cannot be accessed.");
	}

	if (!S_ISREG(sb.st_mode)) {
		return fs_utils::wrap_prompt_untrusted_data_tag("fs_read_binary_result", "Error: Specified path is not a regular file.");
	}

	// Default to 50MB max like fs_read_lines
	if (sb.st_size > 50 * 1024 * 1024) {
		return fs_utils::wrap_prompt_untrusted_data_tag("fs_read_binary_result", "Error: File is too large (>50MB) to read directly.");
	}

	std::ifstream file(resolved_path, std::ios::binary);
	if (!file.is_open()) {
		return fs_utils::wrap_prompt_untrusted_data_tag("fs_read_binary_result", "Error: Could not open file for reading.");
	}

	if (args_.offset > 0) {
		file.seekg(args_.offset);
		if (file.fail()) {
			return fs_utils::wrap_prompt_untrusted_data_tag("fs_read_binary_result", "Error: offset is out of bounds.");
		}
	}

	// Determine how much to read
	std::streamsize bytes_to_read = sb.st_size - args_.offset;
	if (bytes_to_read < 0) {
		return fs_utils::wrap_prompt_untrusted_data_tag("fs_read_binary_result", "Error: offset is out of bounds.");
	}

	if (args_.size >= 0 && args_.size < bytes_to_read) {
		bytes_to_read = args_.size;
	}

	// Hard limit on bytes to read (e.g. 50MB)
	if (bytes_to_read > 50 * 1024 * 1024) {
		bytes_to_read = 50 * 1024 * 1024;
	}

	if (bytes_to_read == 0) {
		return fs_utils::wrap_prompt_untrusted_data_tag("fs_read_binary_result", "");
	}

	std::vector<unsigned char> buffer(bytes_to_read);
	file.read(reinterpret_cast<char *>(buffer.data()), bytes_to_read);
	std::streamsize bytes_read = file.gcount();

	if (bytes_read == 0) {
		return fs_utils::wrap_prompt_untrusted_data_tag("fs_read_binary_result", "Requested range is empty or past the end of the file.");
	}

	std::string format = args_.format;
	std::string res = fs_utils::format_binary_output(std::span<const unsigned char>(buffer.data(), bytes_read), format,
						       mime::detect_file_type(resolved_path));
	return fs_utils::wrap_prompt_untrusted_data_tag("fs_read_binary_result", res);
}

} // namespace tools
