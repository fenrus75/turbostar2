#include <filesystem>
#include <format>
#include "agentlib/tool_context.h"
#include "images/image_manager.h"
#include "mime.h"
#include "image_export.h"

#ifdef HAS_GRAPHICSMAGICK
#include <Magick++.h>
#endif

namespace tools
{

image_export_tool::image_export_tool(image_export_args args)
    : llm_tool_action("Exporting image"), args_(std::move(args))
{
    interaction_ = std::make_shared<agentlib::interaction_image_tool>("image_export", "image_export(uri=" + args_.name + ")", args_.name);
}

bool image_export_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string image_export_tool::execute(agentlib::tool_context &ctx)
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

		std::string src_path = resolve_image_uri(args_.name);
		if (src_path.empty()) {
			set_failure(ctx, "Failed to resolve VFS source image: " + args_.name);
			return "Error: Source image could not be resolved in VFS database.";
		}

		// Ensure parent directory of output exists
		std::filesystem::path dest_path(args_.safe_path);
		std::filesystem::create_directories(dest_path.parent_path());

		// Capture source dimensions for the success report (best-effort; stays 0 if not readable).
		int src_w = 0;
		int src_h = 0;

#ifdef HAS_GRAPHICSMAGICK
		// Attempt to decode-and-write via GraphicsMagick (which also yields dimensions
		// for the report). If the source is not a decodable image (e.g. a raw copied
		// blob), fall back to a byte-for-byte copy and leave dimensions unreported.
		try {
			Magick::InitializeMagick(nullptr);
			Magick::Image img(src_path);
			src_w = img.columns();
			src_h = img.rows();
			img.write(dest_path.string());
		} catch (...) {
			std::filesystem::create_directories(dest_path.parent_path());
			std::filesystem::copy_file(src_path, dest_path, std::filesystem::copy_options::overwrite_existing);
		}
#else
		std::filesystem::copy_file(src_path, dest_path, std::filesystem::copy_options::overwrite_existing);
#endif

		set_success(ctx, "Exported image");

		// Assemble a confidence-building report: resolved absolute destination path,
		// byte size on disk, MIME type detected from the written file, and dimensions.
		std::string dims;
		if (src_w > 0 && src_h > 0) {
			dims = std::format(" ({}x{})", src_w, src_h);
		}

		std::string result_msg = std::format("Successfully exported image{} ({} bytes, {}) to {}.",
						    dims, std::filesystem::file_size(dest_path),
						    mime::detect_file_type(dest_path.string()),
						    std::filesystem::absolute(dest_path).string());
		interaction_->set_result(result_msg);
		return result_msg;
	} catch (const std::exception &e) {
		set_failure(ctx, e.what());
		std::string result_msg = "Error: " + std::string(e.what());
		interaction_->set_result(result_msg);
		return result_msg;
	}
}

} // namespace tools
