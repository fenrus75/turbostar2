#pragma once
#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include "ui/ui_element.h"

class ui_textbox : public ui_element
{
      public:
	ui_textbox(std::string name, int x, int y, int width, const std::string &initial_text,
		   std::function<void(const std::string &)> on_submit = nullptr, std::string label = "");
	ui_textbox(std::string name, int width, const std::string &initial_text,
		   std::function<void(const std::string &)> on_submit = nullptr, std::string label = "");

	void draw(int abs_x, int abs_y) const override;
	bool handle_event(const editor_event &ev, int abs_x, int abs_y) override;
	std::optional<std::string> get_value(const std::string &target_name) const override;
	bool is_focusable() const override
	{
		return true;
	}
	void set_focus(bool focus) override;

	void set_buffer(const std::string &buf)
	{
		buffer_ = buf;
		cursor_pos_ = buffer_.length();
	}
	void set_autocomplete_provider(std::function<std::string(const std::string &)> provider)
	{
		autocomplete_provider_ = std::move(provider);
	}
	void get_selection_range(int &start, int &end) const
	{
		start = selection_start_;
		end = selection_end_;
	}

	void set_history_enabled(bool enabled, const std::string &history_id)
	{
		history_enabled_ = enabled;
		history_id_ = history_id;
	}

      private:
	bool history_enabled_{false};
	std::string history_id_;
	int history_index_{-1};
	std::unordered_map<int, std::string> traversal_edits_;

	std::string buffer_;
	int cursor_pos_;
	std::function<void(const std::string &)> on_submit_;
	std::function<std::string(const std::string &)> autocomplete_provider_;
	std::string label_;
	int selection_start_{-1};
	int selection_end_{-1};
	bool is_mouse_selecting_{false};
};
