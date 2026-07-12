#include "plugins/image_basic/image_resize_tool.h"
#include <Magick++.h>
#include "images/image_manager.h"
#include <exception>

namespace tools
{

image_resize_tool::image_resize_tool(image_resize_args args)
    : llm_tool_action("Resizing image"), args_(std::move(args))
{
    interaction_ = std::make_shared<agentlib::interaction_image_tool>("image_resize", "image_resize(uri=" + args_.original_uri + ")", args_.original_uri);
}

bool image_resize_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string image_resize_tool::execute(agentlib::tool_context &ctx)
{
	try {
		Magick::InitializeMagick(nullptr);

		Magick::Image img(args_.safe_path);
		int orig_w = img.columns();
		int orig_h = img.rows();

		int target_w = orig_w;
		int target_h = orig_h;

		if (args_.ratio.has_value()) {
			target_w = static_cast<int>(orig_w * (*args_.ratio));
			target_h = static_cast<int>(orig_h * (*args_.ratio));
		} else if (args_.newX.has_value() && args_.newY.has_value()) {
			target_w = *args_.newX;
			target_h = *args_.newY;
		} else if (args_.newX.has_value()) {
			target_w = *args_.newX;
			target_h = static_cast<int>(orig_h * (static_cast<double>(target_w) / orig_w));
		} else if (args_.newY.has_value()) {
			target_h = *args_.newY;
			target_w = static_cast<int>(orig_w * (static_cast<double>(target_h) / orig_h));
		}

		if (target_w <= 0 || target_h <= 0) {
			return "Error: Calculated target dimensions must be positive (got " +
			       std::to_string(target_w) + "x" + std::to_string(target_h) + ").";
		}

		img.zoom(Magick::Geometry(target_w, target_h));

		std::string temp_out = images::image_manager::get_instance().get_temp_image_path();
		img.write(temp_out);

		std::string target_alias = args_.original_uri;
		if (args_.output.has_value() && !args_.output->empty()) {
			target_alias = *args_.output;
		}

		std::string new_uri = images::image_manager::get_instance().ingest_image(temp_out, target_alias);
		if (new_uri.empty()) {
			set_failure(ctx, "Failed to ingest resized image to VFS.");
			return "Error: Failed to re-ingest resized image into VFS cache.";
		}

		set_success(ctx, "Resized image to " + std::to_string(target_w) + "x" + std::to_string(target_h));
		std::string result_msg = "Successfully resized image to " + std::to_string(target_w) + "x" + std::to_string(target_h) + ". New URI: " + new_uri;
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
