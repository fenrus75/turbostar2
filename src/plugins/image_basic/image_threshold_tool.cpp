#include "plugins/image_basic/image_threshold_tool.h"
#include <Magick++.h>
#include "images/image_manager.h"
#include <exception>

namespace tools
{

image_threshold_tool::image_threshold_tool(image_threshold_args args)
    : llm_tool_action("Thresholding image"), args_(std::move(args))
{
}

bool image_threshold_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string image_threshold_tool::execute(agentlib::tool_context &ctx)
{
	try {
		Magick::InitializeMagick(nullptr);

		Magick::Image img(args_.safe_path);

		if (args_.level.has_value()) {
			img.threshold(*args_.level);
		} else {
			img.adaptiveThreshold(args_.windowWidth, args_.windowHeight, args_.offset);
		}

		std::string temp_out = images::image_manager::get_instance().get_temp_image_path();
		img.write(temp_out);

		std::string target_alias = args_.name;
		if (args_.output.has_value() && !args_.output->empty()) {
			target_alias = *args_.output;
		}

		std::string new_uri = images::image_manager::get_instance().ingest_image(temp_out, target_alias);
		if (new_uri.empty()) {
			set_failure(ctx, "Failed to ingest thresholded image to VFS.");
			return "Error: Failed to re-ingest thresholded image into VFS cache.";
		}

		std::string threshold_type = args_.level.has_value() ? "standard" : "adaptive";
		set_success(ctx, "Applied " + threshold_type + " thresholding to image");
		return "Successfully applied " + threshold_type + " thresholding to image. New URI: " + new_uri;

	} catch (const Magick::Exception &e) {
		set_failure(ctx, e.what());
		return "GraphicsMagick Error: " + std::string(e.what());
	} catch (const std::exception &e) {
		set_failure(ctx, e.what());
		return "Error: " + std::string(e.what());
	}
}

} // namespace tools
