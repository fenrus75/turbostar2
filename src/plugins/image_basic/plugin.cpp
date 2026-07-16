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

		// Downscale to sub-pixel grid: 2x2 sub-pixels per cell
		int req_width = width * 2;
		int req_height = height * 2;

		double dX = img_cols / (1.0 * req_width);
		double dY = img_rows / (1.0 * req_height);

		// Account for 2:1 character cell aspect ratio. Since subpixels are 2x as tall
		// as they are wide, we need dY = 2 * dX to render with correct 1:1 physical aspect ratio.
		double dY_adj = dY / 2.0;
		if (dX > dY_adj)
			dY_adj = dX;
		else
			dX = dY_adj;
		dY = 2.0 * dY_adj;

		struct temp_pixel_data {
			double R = 0.0, G = 0.0, B = 0.0;
			double count = 0.0;
		};

		std::vector<std::vector<temp_pixel_data>> pixeldata(req_height, std::vector<temp_pixel_data>(req_width));

		for (int y = 0; y < img_rows; y++) {
			for (int x = 0; x < img_cols; x++) {
				int cX = x / dX;
				int cY = y / dY;

				if (cX >= 0 && cX < req_width && cY >= 0 && cY < req_height) {
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
		out["format"] = "quad_v1";
		out["width"] = width;
		out["height"] = height;
		nlohmann::json json_cells = nlohmann::json::array();

		struct rgb {
			double r, g, b;
		};

		struct partition {
			uint8_t mask;
			std::string ch;
		};

		static const partition partitions[8] = {
			{0b0000, " "},
			{0b0001, "▘"},
			{0b0010, "▝"},
			{0b0100, "▖"},
			{0b1000, "▗"},
			{0b0011, "▀"},
			{0b0101, "▌"},
			{0b1001, "▚"}
		};

		auto quantize = [](double val) -> int {
			int int_val = std::clamp(static_cast<int>(val), 0, 255);
			return std::clamp(((int_val + 15) / 32) * 32, 0, 255);
		};

		for (int cy = 0; cy < height; ++cy) {
			for (int cx = 0; cx < width; ++cx) {
				int x0 = cx * 2;
				int x1 = cx * 2 + 1;
				int y0 = cy * 2;
				int y1 = cy * 2 + 1;

				x0 = std::clamp(x0, 0, req_width - 1);
				x1 = std::clamp(x1, 0, req_width - 1);
				y0 = std::clamp(y0, 0, req_height - 1);
				y1 = std::clamp(y1, 0, req_height - 1);

				auto get_rgb = [&](int x_pos, int y_pos) -> rgb {
					double cnt = pixeldata[y_pos][x_pos].count;
					if (cnt <= 0.0) return {0.0, 0.0, 0.0};
					return {
						pixeldata[y_pos][x_pos].R / cnt,
						pixeldata[y_pos][x_pos].G / cnt,
						pixeldata[y_pos][x_pos].B / cnt
					};
				};

				rgb pixels[4];
				pixels[0] = get_rgb(x0, y0); // TL
				pixels[1] = get_rgb(x1, y0); // TR
				pixels[2] = get_rgb(x0, y1); // BL
				pixels[3] = get_rgb(x1, y1); // BR

				double min_error = -1.0;
				int best_fg[3] = {0, 0, 0};
				int best_bg[3] = {0, 0, 0};
				std::string best_char = " ";

				for (int p = 0; p < 8; ++p) {
					uint8_t mask = partitions[p].mask;

					double r1 = 0, g1 = 0, b1 = 0;
					int count1 = 0;
					double r2 = 0, g2 = 0, b2 = 0;
					int count2 = 0;

					for (int i = 0; i < 4; ++i) {
						if (mask & (1 << i)) {
							r1 += pixels[i].r;
							g1 += pixels[i].g;
							b1 += pixels[i].b;
							count1++;
						} else {
							r2 += pixels[i].r;
							g2 += pixels[i].g;
							b2 += pixels[i].b;
							count2++;
						}
					}

					int q_fg[3] = {0, 0, 0};
					if (count1 > 0) {
						q_fg[0] = quantize(r1 / count1);
						q_fg[1] = quantize(g1 / count1);
						q_fg[2] = quantize(b1 / count1);
					}

					int q_bg[3] = {0, 0, 0};
					if (count2 > 0) {
						q_bg[0] = quantize(r2 / count2);
						q_bg[1] = quantize(g2 / count2);
						q_bg[2] = quantize(b2 / count2);
					}

					double error = 0.0;
					for (int i = 0; i < 4; ++i) {
						if (mask & (1 << i)) {
							error += (pixels[i].r - q_fg[0]) * (pixels[i].r - q_fg[0]) +
							         (pixels[i].g - q_fg[1]) * (pixels[i].g - q_fg[1]) +
							         (pixels[i].b - q_fg[2]) * (pixels[i].b - q_fg[2]);
						} else {
							error += (pixels[i].r - q_bg[0]) * (pixels[i].r - q_bg[0]) +
							         (pixels[i].g - q_bg[1]) * (pixels[i].g - q_bg[1]) +
							         (pixels[i].b - q_bg[2]) * (pixels[i].b - q_bg[2]);
						}
					}

					if (min_error < 0.0 || error < min_error) {
						min_error = error;
						best_char = partitions[p].ch;
						best_fg[0] = q_fg[0];
						best_fg[1] = q_fg[1];
						best_fg[2] = q_fg[2];
						best_bg[0] = q_bg[0];
						best_bg[1] = q_bg[1];
						best_bg[2] = q_bg[2];
					}
				}

				nlohmann::json cell = nlohmann::json::array();
				cell.push_back({best_fg[0], best_fg[1], best_fg[2]});
				cell.push_back({best_bg[0], best_bg[1], best_bg[2]});
				cell.push_back(best_char);

				json_cells.push_back(cell);
			}
		}
		out["cells"] = json_cells;
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
	agentlib::tool_registry::get_instance().register_tool_family(
		"image",
		"Activate when performing image manipulation or editing",
		"The 'images://' namespace represents a virtual scratch space for temporary image files created or edited by tools. These virtual image paths do not correspond directly to real files in your git workspace and will not affect the git project unless exported.\n\n"
		"Key Concepts:\n"
		"- VFS URI: Content-addressed paths like 'images://by-sha256/<hash>' uniquely identify images by their SHA-256 hash.\n"
		"- Alias: A friendly label (e.g. 'logo') mapped to a VFS URI. You can assign aliases when importing or editing, allowing you to reference images by simple names.\n\n"
		"Workflow Chaining Example:\n"
		"1. Import a local image (path relative to project root) and assign the alias 'logo':\n"
		"   image_import(filename: 'logo.jpg', output: 'logo')\n"
		"   => Returns: 'images://by-sha256/<hash>'\n"
		"2. Convert the imported image to grayscale, saving the result under the new alias 'logo_gray':\n"
		"   image_grayscale(name: 'logo', output: 'logo_gray')\n"
		"   => Returns: 'images://by-sha256/<new-hash>'\n"
		"3. Export the edited image to a local file path relative to the project root (overwriting is permitted):\n"
		"   image_export(name: 'logo_gray', filename: 'output/logo_gray.png')"
	);
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
