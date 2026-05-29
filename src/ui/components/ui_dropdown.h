#pragma once
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include "ui/ui_element.h"

class ui_dropdown : public ui_element
{
      public:
	ui_dropdown(std::string name, int x, int y, int width, const std::string &initial_text, const std::vector<std::string> &candidates,
		    std::function<void(const std::string &)> on_change = nullptr);

	void draw(int abs_x, int abs_y) const override;
	bool handle_event(const editor_event &ev, int abs_x, int abs_y) override;
	std::optional<std::string> get_value(const std::string &target_name) const override;

	bool has_overlay() const override;
	void draw_overlay(int abs_x, int abs_y) const override;

	void set_buffer(const std::string &buf)
	{
		buffer_ = buf;
		cursor_pos_ = buffer_.length();
	}
	void set_candidates(const std::vector<std::string> &candidates);

      private:
	std::string buffer_;
	int cursor_pos_;
	std::vector<std::string> candidates_;
	bool is_open_{false};
	int selected_candidate_index_{0};
	int scroll_top_{0};
	std::function<void(const std::string &)> on_change_;
};
