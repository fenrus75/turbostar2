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
	cells_.clear();
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
	cells_.clear();
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

	// The 2x2 quadrant rendering maps each character cell to a 2x2 grid of subpixels.
	// We pass the display cell dimensions directly to the downsampling filter.
	int req_width = width_;
	int req_height = height_;

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
		
		auto raw_cells = out["cells"];
		for (const auto &item : raw_cells) {
			thumbnail_cell cell;
			cell.fg_r = item[0][0].get<int>();
			cell.fg_g = item[0][1].get<int>();
			cell.fg_b = item[0][2].get<int>();
			cell.bg_r = item[1][0].get<int>();
			cell.bg_g = item[1][1].get<int>();
			cell.bg_b = item[1][2].get<int>();
			cell.ch = item[2].get<std::string>();
			cells_.push_back(cell);
		}
		has_data_ = true;
	} catch (const std::exception &e) {
		// Parsing failed
		cells_.clear();
		grid_width_ = 0;
		grid_height_ = 0;
		has_data_ = false;
	}
}

void ui_thumbnail::draw(int abs_x, int abs_y) const
{
	if (!has_data_ || cells_.empty() || grid_width_ <= 0 || grid_height_ <= 0) {
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
				
				mvaddch(abs_y + r, abs_x + c, ch);
			}
		}
		std::string label = "[No Preview]";
		int label_x = (width_ - (int)label.length()) / 2;
		int label_y = height_ / 2;
		if (label_x >= 0 && label_x + (int)label.length() <= width_ && label_y >= 0 && label_y < height_) {
			mvaddstr(abs_y + label_y, abs_x + label_x, label.c_str());
		}
		return;
	}

	// Draw high-resolution quadrant thumbnail
	for (int y = 0; y < height_; ++y) {
		for (int x = 0; x < width_; ++x) {
			thumbnail_cell cell;
			if (x < grid_width_ && y < grid_height_) {
				size_t idx = y * grid_width_ + x;
				if (idx < cells_.size()) {
					cell = cells_[idx];
				}
			}

			int fg_col = dynamic_colors::dynamic_get_color(cell.fg_r, cell.fg_g, cell.fg_b);
			int bg_col = dynamic_colors::dynamic_get_color(cell.bg_r, cell.bg_g, cell.bg_b);

			int pair_idx = dynamic_colors::dynamic_alloc_pair(fg_col, bg_col);

			wchar_t wc = L' ';
			if (cell.ch == " ") wc = L' ';
			else if (cell.ch == "▘") wc = 0x2598;
			else if (cell.ch == "▝") wc = 0x259D;
			else if (cell.ch == "▖") wc = 0x2596;
			else if (cell.ch == "▗") wc = 0x2597;
			else if (cell.ch == "▀") wc = 0x2580;
			else if (cell.ch == "▌") wc = 0x258C;
			else if (cell.ch == "▚") wc = 0x259A;

			cchar_t wch;
			wchar_t wstr[2] = { wc, 0 };
			setcchar(&wch, wstr, A_NORMAL, pair_idx, NULL);
			mvadd_wch(abs_y + y, abs_x + x, &wch);
		}
	}
}
