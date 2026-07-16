#include <filesystem>
#include "agentlib/tool_context.h"
#include "images/image_manager.h"
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

#ifdef HAS_GRAPHICSMAGICK
		try {
			Magick::InitializeMagick(nullptr);
			Magick::Image img(src_path);
			img.write(dest_path.string());
		} catch (...) {
			std::filesystem::copy_file(src_path, dest_path, std::filesystem::copy_options::overwrite_existing);
		}
#else
		std::filesystem::copy_file(src_path, dest_path, std::filesystem::copy_options::overwrite_existing);
#endif

		set_success(ctx, "Exported image");
		std::string result_msg = "Successfully exported image to " + args_.original_filename;
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
