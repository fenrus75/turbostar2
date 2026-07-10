#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "event_queue.h"

class ui_container;

/*

# subclasses of ui_element

| subclass          | filename                                                |
| ----------------- | ------------------------------------------------------- |
| ui_button         | src/ui/components/ui_button.h                           |
| ui_buttons_horizontal | src/ui/components/ui_buttons_horizontal.h               |
| ui_buttons_vertical   | src/ui/components/ui_buttons_vertical.h                 |
| ui_checkbox       | src/ui/components/ui_checkbox.h                         |
| ui_dropdown       | src/ui/components/ui_dropdown.h                         |
| ui_fileselector   | src/ui/components/ui_fileselector.h                     |
| ui_group_box      | src/ui/components/ui_group_box.h                         |
| ui_listbox        | src/ui/components/ui_listbox.h                           |
| ui_multiline_edit | src/ui/components/ui_multiline_edit.h                   |
| ui_radio          | src/ui/components/ui_radio.h                            |
| ui_text_label     | src/ui/components/ui_text_label.h                        |
| ui_textbox        | src/ui/components/ui_textbox.h                           |
| ui_horizontal_flow | src/ui/components/ui_horizontal_flow.h                   |
| ui_vertical_flow   | src/ui/components/ui_vertical_flow.h                     |
| ui_durmovie       | src/ui/components/ui_durmovie.h                         |
| ui_color_picker   | src/ui/components/ui_color_picker.h                     |
| ui_thumbnail      | src/ui/components/ui_thumbnail.h                        |
| ui_container      | src/ui/ui_element.h                                     |

*/
class ui_element
{
      public:
	ui_element(std::string name, int x, int y, int width, int height)
	    : name_(std::move(name)), x_(x), y_(y), width_(width), height_(height)
	{
	}
	virtual ~ui_element() = default;

	int x() const
	{
		return x_;
	}
	int y() const
	{
		return y_;
	}
	int width() const
	{
		return width_;
	}
	int height() const
	{
		return height_;
	}
	virtual int natural_width() const
	{
		return width_;
	}
	virtual int natural_height() const
	{
		return height_;
	}
	void set_bounds(int x, int y, int width, int height)
	{
		x_ = x;
		y_ = y;
		width_ = width;
		height_ = height;
	}
	void set_position(int x, int y)
	{
		x_ = x;
		y_ = y;
	}
	virtual void set_width(int width)
	{
		width_ = width;
	}
	virtual void set_height(int height)
	{
		height_ = height;
	}
	// Computes internal layouts and returns true if child sizes or positions changed.
	// RULE: Any subclass that overrides flow() to layout its children dynamically
	// MUST also override natural_width() and natural_height() to correctly report
	// its desired dimensions before actual layout allocation occurs.
	virtual bool flow()
	{
		return false;
	}
	// Updates dynamic animations. Returns true if a redraw is needed.
	virtual bool update_animation()
	{
		return false;
	}
	virtual bool want_horizontal_stretch() const
	{
		return false;
	}
	std::string name() const
	{
		return name_;
	}

	// CRITICAL WARNING FOR SUBCLASS DRAW IMPLEMENTATIONS:
	// The `abs_x` and `abs_y` parameters ALREADY include the element's relative offsets `x_` and `y_`.
	// - DO NOT add `x_` or `y_` to `abs_x` or `abs_y` when rendering (e.g. `abs_y + y_` is WRONG!).
	// - Doing so causes a double-translation bug, rendering the element twice as far down/right as intended.
	virtual void draw(int abs_x, int abs_y) const = 0;

	// CRITICAL WARNING FOR SUBCLASS EVENT HANDLER IMPLEMENTATIONS:
	// The `abs_x` and `abs_y` parameters ALREADY include the element's relative offsets `x_` and `y_`.
	// - Use `abs_x` and `abs_y` directly as the absolute top-left coordinate of the widget for hit testing.
	// - DO NOT add `x_` or `y_` to `abs_x` or `abs_y` when checking mouse bounds.
	virtual bool handle_event(const editor_event &ev, int abs_x, int abs_y) = 0;

	virtual bool has_overlay() const
	{
		return false;
	}
	virtual void draw_overlay(int abs_x, int abs_y) const
	{
		(void)abs_x;
		(void)abs_y;
	}

	virtual std::optional<std::string> get_value(const std::string &target_name) const
	{
		if (name_ == target_name) {
			// By default, no value unless overridden
			return std::nullopt;
		}
		return std::nullopt;
	}

	bool has_focus() const
	{
		return has_focus_;
	}
	virtual void set_focus(bool focus)
	{
		has_focus_ = focus;
	}
	virtual bool is_focusable() const
	{
		return false;
	}
	virtual std::vector<ui_element *> get_focusable_elements()
	{
		if (is_focusable()) {
			return {this};
		}
		return {};
	}
	virtual bool focus_next()
	{
		return false;
	}
	virtual bool focus_previous()
	{
		return false;
	}
	virtual bool focus_first()
	{
		return false;
	}
	virtual bool focus_last()
	{
		return false;
	}

	bool is_pressed() const
	{
		return is_pressed_;
	}
	virtual void set_pressed(bool pressed)
	{
		is_pressed_ = pressed;
	}

	bool press_on_esc() const
	{
		return press_on_esc_;
	}
	void set_press_on_esc(bool val)
	{
		press_on_esc_ = val;
	}

	virtual std::optional<std::string> get_pressed_element_name() const
	{
		if (is_pressed_)
			return name_;
		return std::nullopt;
	}

	void set_parent(ui_container *parent)
	{
		parent_ = parent;
	}
	ui_container *parent() const
	{
		return parent_;
	}

	virtual bool contains_coordinate(int target_x, int target_y, int my_abs_x, int my_abs_y) const
	{
		return target_x >= my_abs_x && target_x < my_abs_x + width_ && target_y >= my_abs_y && target_y < my_abs_y + height_;
	}

      protected:
	std::string name_;
	int x_, y_, width_, height_;
	bool has_focus_{false};
	bool is_pressed_{false};
	bool press_on_esc_{false};
	ui_container *parent_{nullptr};
};

/*

# subclasses of ui_container

| subclass              | filename                                                    |
| --------------------- | ----------------------------------------------------------- |
| ui_buttons_horizontal | src/ui/components/ui_buttons_horizontal.h                   |
| ui_buttons_vertical   | src/ui/components/ui_buttons_vertical.h                     |
| ui_horizontal_flow    | src/ui/components/ui_horizontal_flow.h                     |
| ui_vertical_flow      | src/ui/components/ui_vertical_flow.h                       |
| ui_radiobutton_group  | src/ui/components/ui_radio.h                                |
| ui_checkbox_group     | src/ui/components/ui_checkbox_group.h                       |
| ui_group_box          | src/ui/components/ui_group_box.h                           |
| ui_grid_flow          | src/ui/components/ui_grid_flow.h                            |
| ui_agent_tile         | src/ui/components/ui_agent_tile.h                           |

*/
class ui_container : public ui_element
{
      public:
	ui_container(std::string name, int x, int y, int width, int height) : ui_element(std::move(name), x, y, width, height)
	{
	}

	void add_child(std::unique_ptr<ui_element> child);
	void clear_children();

	void draw(int abs_x, int abs_y) const override;
	bool handle_event(const editor_event &ev, int abs_x, int abs_y) override;
	std::optional<std::string> get_value(const std::string &target_name) const override;
	std::optional<std::string> get_pressed_element_name() const override;

	bool has_overlay() const override;
	void draw_overlay(int abs_x, int abs_y) const override;

	void set_focus(bool focus) override;

	bool focus_next() override;
	bool focus_first() override;
	bool focus_previous() override;
	bool focus_last() override;
	bool flow() override;
	bool update_animation() override;
	virtual void child_got_selected(ui_element *child);
	virtual void set_focus_by_name(const std::string &child_name);
	void set_focused_child(ui_element *child)
	{
		focused_child_ = child;
	}
	ui_element *focused_child() const
	{
		return focused_child_;
	}
	const std::vector<std::unique_ptr<ui_element>> &children() const
	{
		return children_;
	}
	std::vector<ui_element *> get_focusable_elements() override;

      protected:
	std::vector<std::unique_ptr<ui_element>> children_;
	ui_element *focused_child_{nullptr};
};

namespace ui_utils
{
enum class border_style { single, double_line };

void draw_border(int x, int y, int width, int height, border_style style, int color_pair = -1);
} // namespace ui_utils

// Represents a single-line text input field.

#include "ui/components/ui_agent_tile.h"
#include "ui/components/ui_button.h"
#include "ui/components/ui_buttons_horizontal.h"
#include "ui/components/ui_buttons_vertical.h"
#include "ui/components/ui_checkbox.h"
#include "ui/components/ui_checkbox_group.h"
#include "ui/components/ui_color_picker.h"
#include "ui/components/ui_fileselector.h"
#include "ui/components/ui_grid_flow.h"
#include "ui/components/ui_group_box.h"
#include "ui/components/ui_horizontal_flow.h"
#include "ui/components/ui_radio.h"
#include "ui/components/ui_text_label.h"
#include "ui/components/ui_textbox.h"
#include "ui/components/ui_vertical_flow.h"
