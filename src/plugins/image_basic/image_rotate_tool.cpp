#include "plugins/image_basic/image_rotate_tool.h"
#include <Magick++.h>
#include "images/image_manager.h"
#include <exception>

namespace tools
{

image_rotate_tool::image_rotate_tool(image_rotate_args args)
    : llm_tool_action("Rotating image"), args_(std::move(args))
{
    interaction_ = std::make_shared<agentlib::interaction_image_tool>("image_rotate", "image_rotate(uri=" + args_.name + ")", args_.name);
}

bool image_rotate_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string image_rotate_tool::execute(agentlib::tool_context &ctx)
{
	try {
		Magick::InitializeMagick(nullptr);

		Magick::Image img(args_.safe_path);
		img.rotate(args_.degrees);

		std::string temp_out = images::image_manager::get_instance().get_temp_image_path();
		img.write(temp_out);

		std::string target_alias = args_.name;
		if (args_.output.has_value() && !args_.output->empty()) {
			target_alias = *args_.output;
		}

		std::string origin_ops = std::format("rotate({})", args_.degrees);
		std::string new_uri = images::image_manager::get_instance().ingest_image(temp_out, target_alias, args_.name, origin_ops);
		if (new_uri.empty()) {
			set_failure(ctx, "Failed to ingest rotated image to VFS.");
			return "Error: Failed to re-ingest rotated image into VFS cache.";
		}

		set_success(ctx, "Rotated image");
		std::string result_msg = "Successfully rotated image. New URI: " + new_uri;
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
