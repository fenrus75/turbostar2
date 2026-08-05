#include "plugins/image_basic/image_compose_tool.h"
#include <Magick++.h>
#include "images/image_manager.h"
#include <exception>

namespace tools
{

image_compose_tool::image_compose_tool(image_compose_args args)
    : llm_tool_action("Composing images"), args_(std::move(args))
{
	interaction_ = std::make_shared<agentlib::interaction_image_tool>("image_compose", "image_compose(main=" + args_.main_image + ", small=" + args_.small_image + ")", args_.main_image);
}

bool image_compose_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string image_compose_tool::execute(agentlib::tool_context &ctx)
{
	try {
		Magick::InitializeMagick(nullptr);

		Magick::Image main_img(args_.safe_path_main);
		Magick::Image small_img(args_.safe_path_small);

		int main_w = main_img.columns();
		int main_h = main_img.rows();
		int small_w = small_img.columns();
		int small_h = small_img.rows();

		if (args_.x >= main_w || args_.y >= main_h) {
			return "Error: Composition offsets (x=" + std::to_string(args_.x) + ", y=" + std::to_string(args_.y) +
			       ") are out of main image boundaries (" + std::to_string(main_w) + "x" + std::to_string(main_h) + ").";
		}

		int placed_w = std::min(small_w, main_w - args_.x);
		int placed_h = std::min(small_h, main_h - args_.y);

		int bbox_x_start = args_.x;
		int bbox_x_end = args_.x + placed_w - 1;
		int bbox_y_start = args_.y;
		int bbox_y_end = args_.y + placed_h - 1;

		main_img.composite(small_img, args_.x, args_.y, Magick::OverCompositeOp);

		std::string temp_out = images::image_manager::get_instance().get_temp_image_path();
		main_img.write(temp_out);

		std::string target_alias = args_.main_image;
		if (args_.output.has_value() && !args_.output->empty()) {
			target_alias = *args_.output;
		}

		std::string origin_ops = std::format("compose(x={},y={})", args_.x, args_.y);
		std::string new_uri = images::image_manager::get_instance().ingest_image(temp_out, target_alias, args_.main_image, origin_ops);
		if (new_uri.empty()) {
			set_failure(ctx, "Failed to ingest composed image to VFS.");
			return "Error: Failed to re-ingest composed image into VFS cache.";
		}

		set_success(ctx, "Composed images");

		std::string bbox_info = std::format("Inserted 2nd image ({}x{}) onto main image ({}x{}) at bounding box (x={}..{}, y={}..{}) [width={}, height={}].",
					small_w, small_h, main_w, main_h, bbox_x_start, bbox_x_end, bbox_y_start, bbox_y_end, placed_w, placed_h);

		std::string fit_info;
		if (placed_w < small_w || placed_h < small_h) {
			fit_info = std::format("Note: 2nd image exceeded main image boundaries; pixel sub-range (x=0..{}, y=0..{}) of 2nd image was placed.",
					placed_w - 1, placed_h - 1);
		} else {
			fit_info = "100% of 2nd image was placed.";
		}

		std::string result_msg = std::format("Successfully composed images. {} {} New URI: {}", bbox_info, fit_info, new_uri);
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
