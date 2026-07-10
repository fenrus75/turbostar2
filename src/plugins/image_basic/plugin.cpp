#include "agentlib/tool_registry.h"
#include "filter_registry.h"
#include <Magick++.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <vector>

static std::string generate_image_thumbnail_json(const std::string &input_json_str)
{
	try {
		nlohmann::json args = nlohmann::json::parse(input_json_str);
		std::string path = args["path"].get<std::string>();
		int width = args["width"].get<int>();
		int height = args["height"].get<int>();

		if (width <= 0 || height <= 0 || path.empty()) {
			return "{}";
		}

		Magick::InitializeMagick(nullptr);
		Magick::Image image;
		image.quiet(false);
		image.read(path);

		int img_cols = image.columns();
		int img_rows = image.rows();

		double dX = img_cols / (1.0 * width);
		double dY = img_rows / (1.0 * height);

		if (dX > dY)
			dY = dX;
		else
			dX = dY;

		struct temp_pixel_data {
			double R = 0.0, G = 0.0, B = 0.0;
			double count = 0.0;
		};

		std::vector<std::vector<temp_pixel_data>> pixeldata(height, std::vector<temp_pixel_data>(width));

		for (int y = 0; y < img_rows; y++) {
			for (int x = 0; x < img_cols; x++) {
				int cX = x / dX;
				int cY = y / dY;

				if (cX >= 0 && cX < width && cY >= 0 && cY < height) {
					const Magick::PixelPacket *pixel_cache = image.getConstPixels(x, y, 1, 1);
					if (pixel_cache) {
						Magick::Quantum red = pixel_cache->red;
						Magick::Quantum green = pixel_cache->green;
						Magick::Quantum blue = pixel_cache->blue;

						pixeldata[cY][cX].R += (int)(1.0 * red / MaxRGB * 255);
						pixeldata[cY][cX].G += (int)(1.0 * green / MaxRGB * 255);
						pixeldata[cY][cX].B += (int)(1.0 * blue / MaxRGB * 255);
						pixeldata[cY][cX].count += 1.0;
					}
				}
			}
		}

		nlohmann::json out;
		out["width"] = width;
		out["height"] = height;
		nlohmann::json json_pixels = nlohmann::json::array();

		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				double R = pixeldata[y][x].R / (pixeldata[y][x].count + 0.0001);
				double G = pixeldata[y][x].G / (pixeldata[y][x].count + 0.0001);
				double B = pixeldata[y][x].B / (pixeldata[y][x].count + 0.0001);

				int r_val = std::clamp((int)R, 0, 255);
				int g_val = std::clamp((int)G, 0, 255);
				int b_val = std::clamp((int)B, 0, 255);

				json_pixels.push_back({r_val, g_val, b_val});
			}
		}
		out["pixels"] = json_pixels;
		return out.dump();

	} catch (const std::exception &e) {
		return "{}";
	}
}

extern "C" {

const char *plugin_name(void)
{
	return "Basic Image Operations";
}

const char *plugin_description(void)
{
	return "Provides basic image manipulation capabilities (e.g. image_resize).";
}

void register_image_resize(void);
void unregister_image_resize(void);
void register_image_crop(void);
void unregister_image_crop(void);
void register_image_rotate(void);
void unregister_image_rotate(void);
void register_image_mirror(void);
void unregister_image_mirror(void);
void register_image_grayscale(void);
void unregister_image_grayscale(void);
void register_image_threshold(void);
void unregister_image_threshold(void);

void plugin_run(void)
{
	register_image_resize();
	register_image_crop();
	register_image_rotate();
	register_image_mirror();
	register_image_grayscale();
	register_image_threshold();
	agentlib::tool_registry::get_instance().register_tool_family("image", "Activate when performing image manipulation or editing");
	agentlib::filter_registry::get_instance().register_filter("image_thumbnail", generate_image_thumbnail_json, {"image"});
}

void plugin_unload(void)
{
	unregister_image_resize();
	unregister_image_crop();
	unregister_image_rotate();
	unregister_image_mirror();
	unregister_image_grayscale();
	unregister_image_threshold();
	agentlib::tool_registry::get_instance().unregister_tool_family("image");
	agentlib::filter_registry::get_instance().unregister_filter("image_thumbnail");
}

}
