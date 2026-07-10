#include "ui/components/ui_thumbnail.h"
#include "filter_registry.h"
#include "ui/dynamic_colors.h"
#include "images/image_manager.h"
#include <ncursesw/ncurses.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <iostream>



ui_thumbnail::ui_thumbnail(std::string name, int x, int y, int width, int height)
    : ui_element(std::move(name), x, y, width, height)
{
}

void ui_thumbnail::set_image_path(const std::string &image_path)
{
	if (image_path_ != image_path) {
		image_path_ = image_path;
		fetch_thumbnail_data();
	}
}

void ui_thumbnail::clear_image()
{
	image_path_.clear();
	pixels_.clear();
	grid_width_ = 0;
	grid_height_ = 0;
	has_data_ = false;
}

bool ui_thumbnail::handle_event(const editor_event & /*ev*/, int /*abs_x*/, int /*abs_y*/)
{
	return false;
}

void ui_thumbnail::fetch_thumbnail_data()
{
	pixels_.clear();
	grid_width_ = 0;
	grid_height_ = 0;
	has_data_ = false;

	if (image_path_.empty()) {
		return;
	}

	// 1. Resolve to physical path if VFS URI
	std::string physical_path;
	if (image_path_.starts_with("images://")) {
		physical_path = images::image_manager::get_instance().resolve_uri(image_path_);
	} else {
		// Try resolving as alias
		physical_path = images::image_manager::get_instance().resolve_uri("images://by-name/" + image_path_);
		if (physical_path.empty()) {
			physical_path = images::image_manager::get_instance().resolve_uri("images://" + image_path_);
		}
	}

	if (physical_path.empty() && std::filesystem::exists(image_path_)) {
		physical_path = image_path_;
	}

	if (physical_path.empty()) {
		return;
	}

	// 2. Query filter_registry for image_thumbnail filter
	auto &registry = agentlib::filter_registry::get_instance();
	if (!registry.has_filter("image_thumbnail")) {
		return;
	}

	// The half-block character rendering means 1 character row = 2 pixel rows.
	// So we downsample to:
	// - Width of the thumbnail = width_ (columns)
	// - Height of the thumbnail = height_ * 2 (rows of pixels)
	int req_width = width_;
	int req_height = height_ * 2;

	nlohmann::json input_json = {
		{"path", physical_path},
		{"width", req_width},
		{"height", req_height}
	};

	bool success = false;
	std::string result_str = registry.apply_filter("image_thumbnail", input_json.dump(), success);
	if (!success) {
		return;
	}

	try {
		nlohmann::json out = nlohmann::json::parse(result_str);
		grid_width_ = out["width"].get<int>();
		grid_height_ = out["height"].get<int>();
		
		auto raw_pixels = out["pixels"];
		for (const auto &item : raw_pixels) {
			thumbnail_pixel pix;
			pix.r = item[0].get<int>();
			pix.g = item[1].get<int>();
			pix.b = item[2].get<int>();
			pixels_.push_back(pix);
		}
		has_data_ = true;
	} catch (const std::exception &e) {
		// Parsing failed
		pixels_.clear();
		grid_width_ = 0;
		grid_height_ = 0;
		has_data_ = false;
	}
}

void ui_thumbnail::draw(int abs_x, int abs_y) const
{
	if (!has_data_ || pixels_.empty() || grid_width_ <= 0 || grid_height_ <= 0) {
		// Draw a clean placeholder box
		for (int r = 0; r < height_; ++r) {
			for (int c = 0; c < width_; ++c) {
				char ch = ' ';
				if (r == 0 && c == 0) ch = '+';
				else if (r == 0 && c == width_ - 1) ch = '+';
				else if (r == height_ - 1 && c == 0) ch = '+';
				else if (r == height_ - 1 && c == width_ - 1) ch = '+';
				else if (r == 0 || r == height_ - 1) ch = '-';
				else if (c == 0 || c == width_ - 1) ch = '|';
				
				mvaddch(abs_y + y_ + r, abs_x + x_ + c, ch);
			}
		}
		std::string label = "[No Preview]";
		int label_x = x_ + (width_ - (int)label.length()) / 2;
		int label_y = y_ + height_ / 2;
		if (label_x >= x_ && label_x + (int)label.length() <= x_ + width_ && label_y >= y_ && label_y < y_ + height_) {
			mvaddstr(abs_y + label_y, abs_x + label_x, label.c_str());
		}
		return;
	}

	// Draw color thumbnail
	for (int y = 0; y < height_; ++y) {
		for (int x = 0; x < width_; ++x) {
			int top_pixel_y = 2 * y;
			int bottom_pixel_y = 2 * y + 1;

			thumbnail_pixel top_pix{0, 0, 0};
			thumbnail_pixel bottom_pix{0, 0, 0};

			if (x < grid_width_ && top_pixel_y < grid_height_) {
				top_pix = pixels_[top_pixel_y * grid_width_ + x];
			}
			if (x < grid_width_ && bottom_pixel_y < grid_height_) {
				bottom_pix = pixels_[bottom_pixel_y * grid_width_ + x];
			}

			int fg_col = dynamic_colors::dynamic_get_color(bottom_pix.r, bottom_pix.g, bottom_pix.b);
			int bg_col = dynamic_colors::dynamic_get_color(top_pix.r, top_pix.g, top_pix.b);

			int pair_idx = dynamic_colors::dynamic_alloc_pair(fg_col, bg_col);

			attron(COLOR_PAIR(pair_idx));
			mvaddstr(abs_y + y_ + y, abs_x + x_ + x, "▄");
			attroff(COLOR_PAIR(pair_idx));
		}
	}
}


