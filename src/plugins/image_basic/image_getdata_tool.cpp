#include "plugins/image_basic/image_getdata_tool.h"
#include "agentlib/tool_context.h"
#include "images/image_manager.h"
#include "fs_utils.h"
#include "utf8.h"
#include "mime.h"
#include <filesystem>
#include <fstream>
#include <vector>

namespace tools
{

image_getdata_tool::image_getdata_tool(image_getdata_args args)
    : llm_tool_action("Retrieving image data"), args_(std::move(args))
{
}

bool image_getdata_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string image_getdata_tool::execute(agentlib::tool_context &ctx)
{
	try {
		auto resolve_image_uri = [](const std::string &uri) -> std::string {
			if (uri.starts_with("images://")) {
				return images::image_manager::get_instance().resolve_uri(uri);
			}
			std::string resolved = images::image_manager::get_instance().resolve_uri("images://by-name/" + uri);
			if (!resolved.empty()) return resolved;
			return images::image_manager::get_instance().resolve_uri("images://" + uri);
		};

		std::string src_path = resolve_image_uri(args_.filename);
		if (src_path.empty()) {
			set_failure(ctx, "Failed to resolve VFS image: " + args_.filename);
			return "Error: Image could not be resolved in VFS database.";
		}

		size_t limit = args_.max_bytes > 0 ? args_.max_bytes : 51200;

		std::error_code ec;
		uint64_t file_size = std::filesystem::file_size(src_path, ec);
		if (!ec && file_size > limit) {
			std::string err = std::format("Error: Image size ({} bytes) exceeds maximum size limit of {} bytes (~{} KB). Use image_resize, image_crop, or image_thumbnail to reduce the image size before retrieving data.", file_size, limit, limit / 1024);
			set_failure(ctx, err);
			return err;
		}

		std::ifstream file(src_path, std::ios::binary);
		if (!file.is_open()) {
			set_failure(ctx, "Failed to open image file: " + src_path);
			return "Error: Failed to open image file.";
		}

		std::vector<unsigned char> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		if (buffer.size() > limit) {
			std::string err = std::format("Error: Image size ({} bytes) exceeds maximum size limit of {} bytes (~{} KB). Use image_resize, image_crop, or image_thumbnail to reduce the image size before retrieving data.", buffer.size(), limit, limit / 1024);
			set_failure(ctx, err);
			return err;
		}
		std::string mime_type = utf8::detect_mime(std::string_view(reinterpret_cast<const char *>(buffer.data()), buffer.size()));
		if (mime_type.empty() || mime_type == "application/octet-stream" || mime_type == "text/plain") {
			std::string fallback_mime = mime::detect_file_type(args_.filename);
			if (fallback_mime != "application/octet-stream") {
				mime_type = fallback_mime;
			}
		}
		std::string data_url = fs_utils::format_binary_output(buffer, "base64", mime_type);

		// Prepend a compact summary (resolved name, dimensions, byte size, MIME) before the
		// (potentially large) base64 payload so the caller gets bounded, confidence-building
		// context even when the embedded image data is truncated by the environment.
		images::image_metadata meta;
		std::string dims = "(unknown dimensions)";
		if (images::image_manager::get_instance().get_metadata(args_.filename, meta) && meta.width > 0 && meta.height > 0) {
			dims = std::format("{}x{}", meta.width, meta.height);
		}
		std::string header = std::format("Image {} ({}, {} bytes, {})\n", args_.filename, dims, buffer.size(), mime_type);

		set_success(ctx, "Retrieved image data");
		return header + data_url;
	} catch (const std::exception &e) {
		set_failure(ctx, e.what());
		return "Error: " + std::string(e.what());
	}
}

} // namespace tools
