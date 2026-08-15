#include "plugins/image_basic/image_getdata_tool.h"
#include "plugins/image_basic/image_basic_utils.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>
#include "agentlib/tool_context.h"
#include "fs_utils.h"
#include "images/image_manager.h"
#include "mime.h"
#include "utf8.h"

#ifdef HAS_GRAPHICSMAGICK
#include <Magick++.h>
#endif

namespace tools
{

image_getdata_tool::image_getdata_tool(image_getdata_args args) : llm_tool_action("Retrieving image data"), args_(std::move(args))
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
			if (!resolved.empty())
				return resolved;
			return images::image_manager::get_instance().resolve_uri("images://" + uri);
		};

		std::string src_path = resolve_image_uri(args_.filename);
		if (src_path.empty()) {
			set_failure(ctx, "Failed to resolve VFS image: " + args_.filename);
			return "Error: Image could not be resolved in VFS database.";
		}

		size_t limit = args_.max_bytes > 0 ? args_.max_bytes : 51200;

		// The image bytes to encode into the data URL. When thumbnail is requested and
		// the shrink succeeds this holds the shrunk image; otherwise it holds the raw
		// file contents read from disk.
		std::vector<unsigned char> buffer;

		// Optional ephemeral thumbnail: when requested, shrink the image so its largest
		// dimension is at most kThumbnailMaxDim (the other dimension scaled to preserve
		// aspect ratio). This keeps the base64 payload small (well under the default
		// 50 KB limit) while remaining a reasonably sized view. The `thumbnail` option is
		// applied on the decoded in-memory image and is fully ephemeral - it never writes
		// to the VFS cache or mappings, so this tool stays "pure" (read/get only).
		std::error_code ec;
		uint64_t file_size = std::filesystem::file_size(src_path, ec);
		if (!ec && file_size > 50 * 1024 * 1024) {
			return "Error: Image file size exceeds maximum 50 MB limit.";
		}

		// Optional ephemeral thumbnail
		if (args_.thumbnail) {
			std::vector<unsigned char> thumb_buffer;
#ifdef HAS_GRAPHICSMAGICK
			try {
				set_magick_resource_limits();

				Magick::Image img(src_path);
				int w = img.columns();
				int h = img.rows();
				double scale = static_cast<double>(image_getdata_args::kThumbnailMaxDim) / std::max(w, h);
				int tw = static_cast<int>(w * scale);
				int th = static_cast<int>(h * scale);
				if (w > image_getdata_args::kThumbnailMaxDim || h > image_getdata_args::kThumbnailMaxDim) {
					img.zoom(Magick::Geometry(tw, th));
				}
				// Write back in the image's original (autodetected) format so the
				// detected MIME type of the thumbnail matches the original image and the
				// returned data URL is well-formed for the model.
				Magick::Blob blob;
				img.write(&blob);
				thumb_buffer.assign(static_cast<const unsigned char *>(blob.data()),
						    static_cast<const unsigned char *>(blob.data()) + blob.length());
			} catch (const std::exception &e) {
				// If decoding/shrinking fails, fall through to the non-thumbnail path
				// which will re-apply the size check and produce the appropriate error.
				thumb_buffer.clear();
			}
#else
			// No GraphicsMagick support: cannot shrink in-memory, fall through below.
			thumb_buffer.clear();
#endif
			if (!thumb_buffer.empty()) {
				buffer = std::move(thumb_buffer);
			}
		}

		if (buffer.empty() && !ec && file_size > limit) {
			std::string err =
			    std::format("Error: Image size ({} bytes) exceeds maximum size limit of {} bytes (~{} KB). Use image_resize, "
					"image_crop, or image_thumbnail to reduce the image size before retrieving data.",
					file_size, limit, limit / 1024);
			set_failure(ctx, err);
			return err;
		}

		if (buffer.empty()) {
			std::ifstream file(src_path, std::ios::binary);
			if (!file.is_open()) {
				set_failure(ctx, "Failed to open image file: " + src_path);
				return "Error: Failed to open image file.";
			}
			buffer.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		}
		if (buffer.size() > limit) {
			std::string err =
			    std::format("Error: Image size ({} bytes) exceeds maximum size limit of {} bytes (~{} KB). Use image_resize, "
					"image_crop, or image_thumbnail to reduce the image size before retrieving data.",
					buffer.size(), limit, limit / 1024);
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

		// Return the raw data URL with no prepended prose. The OpenAI tool-image
		// specification requires image payloads to be a strict "data URL" (the format
		// produced by format_binary_output below). Any prefix breaks strict parsers and
		// confuses some models/agents, so we return the data URL exactly as-is.
		set_success(ctx, args_.thumbnail ? "Retrieved thumbnail image data" : "Retrieved image data");
		return data_url;
	} catch (const std::exception &e) {
		set_failure(ctx, e.what());
		return "Error: " + std::string(e.what());
	}
}

} // namespace tools
