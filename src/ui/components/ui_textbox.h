#pragma once
#include "ui/ui_element.h"
#include <functional>
#include <string>
#include <optional>
#include <vector>
#include <filesystem>
#include <chrono>

class ui_textbox : public ui_element
{
      public:
	ui_textbox(std::string name, int x, int y, int width, const std::string &initial_text, std::function<void(const std::string&)> on_submit = nullptr);

	void draw(int abs_x, int abs_y) const override;
	bool handle_event(const editor_event &ev, int abs_x, int abs_y) override;
	std::optional<std::string> get_value(const std::string &target_name) const override;
	
	void set_buffer(const std::string& buf) { buffer_ = buf; cursor_pos_ = buffer_.length(); }
	void set_autocomplete_provider(std::function<std::string(const std::string&)> provider) { autocomplete_provider_ = std::move(provider); }

      private:
	std::string buffer_;
	int cursor_pos_;
	std::function<void(const std::string&)> on_submit_;
	std::function<std::string(const std::string&)> autocomplete_provider_;
};
