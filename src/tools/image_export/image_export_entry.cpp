#include <filesystem>
#include "agentlib/tool_context.h"
#include "images/image_manager.h"
#include "image_export.h"

namespace tools
{

image_export_tool::image_export_tool(image_export_args args)
    : llm_tool_action("Exporting image"), args_(std::move(args))
{
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

		std::filesystem::copy_file(src_path, dest_path, std::filesystem::copy_options::overwrite_existing);

		set_success(ctx, "Exported " + args_.name + " to " + args_.original_filename);
		return "Successfully exported VFS image " + args_.name + " to workspace file: " + args_.original_filename;

	} catch (const std::exception &e) {
		set_failure(ctx, e.what());
		return "Error: " + std::string(e.what());
	}
}

} // namespace tools
