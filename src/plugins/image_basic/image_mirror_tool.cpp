#include "plugins/image_basic/image_mirror_tool.h"
#include <Magick++.h>
#include "images/image_manager.h"
#include <exception>

namespace tools
{

image_mirror_tool::image_mirror_tool(image_mirror_args args)
    : llm_tool_action("Mirroring image"), args_(std::move(args))
{
}

bool image_mirror_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string image_mirror_tool::execute(agentlib::tool_context &ctx)
{
	try {
		Magick::InitializeMagick(nullptr);

		Magick::Image img(args_.safe_path);

		if (args_.direction == "vertical") {
			img.flip();
		} else if (args_.direction == "both") {
			img.flip();
			img.flop();
		} else { // default "horizontal"
			img.flop();
		}

		std::string temp_out = images::image_manager::get_instance().get_temp_image_path();
		img.write(temp_out);

		std::string target_alias = args_.name;
		if (args_.output.has_value() && !args_.output->empty()) {
			target_alias = *args_.output;
		}

		std::string new_uri = images::image_manager::get_instance().ingest_image(temp_out, target_alias);
		if (new_uri.empty()) {
			set_failure(ctx, "Failed to ingest mirrored image to VFS.");
			return "Error: Failed to re-ingest mirrored image into VFS cache.";
		}

		set_success(ctx, "Mirrored image " + args_.direction);
		return "Successfully mirrored image (" + args_.direction + "). New URI: " + new_uri;

	} catch (const Magick::Exception &e) {
		set_failure(ctx, e.what());
		return "GraphicsMagick Error: " + std::string(e.what());
	} catch (const std::exception &e) {
		set_failure(ctx, e.what());
		return "Error: " + std::string(e.what());
	}
}

} // namespace tools
