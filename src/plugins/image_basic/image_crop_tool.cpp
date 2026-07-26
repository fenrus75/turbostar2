#include "plugins/image_basic/image_crop_tool.h"
#include <Magick++.h>
#include "images/image_manager.h"
#include <exception>

namespace tools
{

image_crop_tool::image_crop_tool(image_crop_args args)
    : llm_tool_action("Cropping image"), args_(std::move(args))
{
    interaction_ = std::make_shared<agentlib::interaction_image_tool>("image_crop", "image_crop(uri=" + args_.name + ")", args_.name);
}

bool image_crop_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string image_crop_tool::execute(agentlib::tool_context &ctx)
{
	try {
		Magick::InitializeMagick(nullptr);

		Magick::Image img(args_.safe_path);
		int orig_w = img.columns();
		int orig_h = img.rows();

		if (args_.x >= orig_w || args_.y >= orig_h) {
			return "Error: Crop offsets (x=" + std::to_string(args_.x) + ", y=" + std::to_string(args_.y) +
			       ") are out of image boundaries (" + std::to_string(orig_w) + "x" + std::to_string(orig_h) + ").";
		}

		img.crop(Magick::Geometry(args_.width, args_.height, args_.x, args_.y));
		img.page(Magick::Geometry(args_.width, args_.height, 0, 0));

		std::string temp_out = images::image_manager::get_instance().get_temp_image_path();
		img.write(temp_out);

		std::string target_alias = args_.name;
		if (args_.output.has_value() && !args_.output->empty()) {
			target_alias = *args_.output;
		}

		std::string origin_ops = std::format("crop({},{},{},{})", args_.x, args_.y, args_.width, args_.height);
		std::string new_uri = images::image_manager::get_instance().ingest_image(temp_out, target_alias, args_.name, origin_ops);
		if (new_uri.empty()) {
			set_failure(ctx, "Failed to ingest cropped image to VFS.");
			return "Error: Failed to re-ingest cropped image into VFS cache.";
		}

		set_success(ctx, "Cropped image");
		std::string result_msg = "Successfully cropped image. New URI: " + new_uri;
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
