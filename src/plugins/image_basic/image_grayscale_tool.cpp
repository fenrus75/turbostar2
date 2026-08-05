#include "plugins/image_basic/image_grayscale_tool.h"
#include <Magick++.h>
#include <cstddef>
#include <format>
#include "images/image_manager.h"
#include <exception>

namespace tools
{

image_grayscale_tool::image_grayscale_tool(image_grayscale_args args)
    : llm_tool_action("Converting image to grayscale"), args_(std::move(args))
{
    interaction_ = std::make_shared<agentlib::interaction_image_tool>("image_grayscale", "image_grayscale(uri=" + args_.name + ")", args_.name);
}

bool image_grayscale_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string image_grayscale_tool::execute(agentlib::tool_context &ctx)
{
	try {
		Magick::InitializeMagick(nullptr);

		Magick::Image img(args_.safe_path);
		int img_w = img.columns();
		int img_h = img.rows();
		img.type(Magick::GrayscaleType);

		// Compute the average grayscale intensity (0-255 scale) across all pixels. Since
		// grayscale is not binary, average luminance is the natural summary statistic (unlike
		// the white/black percentage used for thresholding). Gamma-aware imaging is not assumed;
		// this gives the mean decoded R=G=B value.
		const size_t total_pixels = static_cast<size_t>(img_w) * static_cast<size_t>(img_h);
		double luminance_sum = 0.0;
		for (int y = 0; y < img_h; ++y) {
			const Magick::PixelPacket *row = img.getConstPixels(0, y, img_w, 1);
			if (!row)
				continue;
			for (int x = 0; x < img_w; ++x) {
				luminance_sum += (static_cast<double>(row[x].red) / MaxRGB) * 255.0;
			}
		}
		int avg_luminance = total_pixels > 0 ? static_cast<int>(luminance_sum / total_pixels + 0.5) : 0;

		std::string temp_out = images::image_manager::get_instance().get_temp_image_path();
		img.write(temp_out);

		std::string target_alias = args_.name;
		if (args_.output.has_value() && !args_.output->empty()) {
			target_alias = *args_.output;
		}

		std::string new_uri = images::image_manager::get_instance().ingest_image(temp_out, target_alias, args_.name, "grayscale");
		if (new_uri.empty()) {
			set_failure(ctx, "Failed to ingest grayscale image to VFS.");
			return "Error: Failed to re-ingest grayscale image into VFS cache.";
		}

		// Report the image dimensions and average luminance (0-255) so the caller can judge
		// the brightness of the resulting grayscale image.
		std::string result_msg = std::format("Successfully converted image to grayscale ({}x{}, average luminance {}/255). New URI: {}",
					    img_w, img_h, avg_luminance, new_uri);
		set_success(ctx, "Converted image to grayscale");
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
