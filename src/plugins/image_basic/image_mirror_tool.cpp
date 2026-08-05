#include "plugins/image_basic/image_mirror_tool.h"
#include <Magick++.h>
#include "images/image_manager.h"
#include <exception>

namespace tools
{

image_mirror_tool::image_mirror_tool(image_mirror_args args)
    : llm_tool_action("Mirroring image"), args_(std::move(args))
{
    interaction_ = std::make_shared<agentlib::interaction_image_tool>("image_mirror", "image_mirror(uri=" + args_.name + ")", args_.name);
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
		int img_w = img.columns();
		int img_h = img.rows();

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

		std::string origin_ops = std::format("mirror({})", args_.direction);
		std::string new_uri = images::image_manager::get_instance().ingest_image(temp_out, target_alias, args_.name, origin_ops);
		if (new_uri.empty()) {
			set_failure(ctx, "Failed to ingest mirrored image to VFS.");
			return "Error: Failed to re-ingest mirrored image into VFS cache.";
		}

		// Mirroring preserves dimensions, so report the chosen direction and the unchanged
		// size to confirm the requested flip applied.
		std::string result_msg = std::format("Successfully mirrored image {} (size unchanged {}x{}). New URI: {}",
					    args_.direction, img_w, img_h, new_uri);
		set_success(ctx, "Mirrored image");
		interaction_->set_output_image(new_uri);
		interaction_->set_result(result_msg);
		return result_msg;

	} catch (const Magick::Exception &e) {
		set_failure(ctx, e.what());
		std::string result_msg = "GraphicsMagick Error: " + std::string(e.what());
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
