#include "plugins/image_basic/image_threshold_tool.h"
#include <Magick++.h>
#include <cstddef>
#include "images/image_manager.h"
#include <exception>

namespace tools
{

image_threshold_tool::image_threshold_tool(image_threshold_args args)
    : llm_tool_action("Applying threshold to image"), args_(std::move(args))
{
    interaction_ = std::make_shared<agentlib::interaction_image_tool>("image_threshold", "image_threshold(uri=" + args_.name + ")", args_.name);
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
		int img_w = img.columns();
		int img_h = img.rows();

		if (args_.level.has_value()) {
			img.threshold(*args_.level);
		} else {
			img.adaptiveThreshold(args_.windowWidth, args_.windowHeight, args_.offset);
		}

		// Thresholding produces a binary (black/white) image, so count the number of
		// "white" pixels (intensity above the midpoint) and report the percentage split to
		// give the caller a concrete sense of the binarized histogram.
		const size_t total_pixels = static_cast<size_t>(img_w) * static_cast<size_t>(img_h);
		size_t white_count = 0;
		for (int y = 0; y < img_h; ++y) {
			const Magick::PixelPacket *row = img.getConstPixels(0, y, img_w, 1);
			if (!row)
				continue;
			for (int x = 0; x < img_w; ++x) {
				if ((static_cast<double>(row[x].red) / MaxRGB) * 255.0 > 127.0) {
					++white_count;
				}
			}
		}
		int white_pct = total_pixels > 0 ? static_cast<int>(100.0 * white_count / total_pixels + 0.5) : 0;

		std::string temp_out = images::image_manager::get_instance().get_temp_image_path();
		img.write(temp_out);

		std::string target_alias = args_.name;
		if (args_.output.has_value() && !args_.output->empty()) {
			target_alias = *args_.output;
		}

		std::string origin_ops = args_.level.has_value() ? std::format("threshold({})", *args_.level) : std::format("adaptiveThreshold({},{},{})", args_.windowWidth, args_.windowHeight, args_.offset);
		std::string new_uri = images::image_manager::get_instance().ingest_image(temp_out, target_alias, args_.name, origin_ops);
		if (new_uri.empty()) {
			set_failure(ctx, "Failed to ingest thresholded image to VFS.");
			return "Error: Failed to re-ingest thresholded image into VFS cache.";
		}

		// Report the threshold variant (fixes a previously unused variable) and the resulting
		// white/black pixel split so the caller can judge the binarized histogram.
		std::string threshold_type = args_.level.has_value() ? "standard" : "adaptive";
		std::string result_msg = std::format("Successfully applied {} threshold filter to image ({}x{}, {}% white / {}% black). New URI: {}",
					    threshold_type, img_w, img_h, white_pct, 100 - white_pct, new_uri);
		set_success(ctx, "Applied threshold to image");
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
