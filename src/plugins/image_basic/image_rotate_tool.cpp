#include "plugins/image_basic/image_rotate_tool.h"
#include "plugins/image_basic/image_basic_utils.h"
#include <Magick++.h>
#include "images/image_manager.h"
#include "fs_utils.h"
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
		set_magick_resource_limits();

		Magick::Image img(args_.safe_path);
		int orig_w = img.columns();
		int orig_h = img.rows();

		img.rotate(args_.degrees);

		int new_w = img.columns();
		int new_h = img.rows();

		std::string temp_out = images::image_manager::get_instance().get_temp_image_path();
		img.write(temp_out);

		std::string target_alias = args_.name;
		if (args_.output.has_value() && !args_.output->empty()) {
			target_alias = *args_.output;
		}

		std::string origin_ops = std::format("rotate({})", args_.degrees);
		std::string new_uri = images::image_manager::get_instance().ingest_image(temp_out, target_alias, args_.name, origin_ops);

		std::error_code ec;
		std::filesystem::remove(temp_out, ec);

		if (new_uri.empty()) {
			set_failure(ctx, "Failed to ingest rotated image to VFS.");
			return "Error: Failed to re-ingest rotated image into VFS cache.";
		}

		std::string result_msg = std::format("Successfully rotated image by {} degrees from {}x{} to {}x{}. New URI: {}",
					    args_.degrees, orig_w, orig_h, new_w, new_h, new_uri);
		set_success(ctx, "Rotated image");
		interaction_->set_output_image(new_uri);
		interaction_->set_result(result_msg);
		return fs_utils::wrap_prompt_untrusted_data_tag("image_rotate_result", result_msg);

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
