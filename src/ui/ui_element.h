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
	void set_bounds(int x, int y, int width, int height)
	{
		x_ = x;
		y_ = y;
		width_ = width;
		height_ = height;
	}
	std::string name() const
	{
		return name_;
	}

	virtual void draw(int abs_x, int abs_y) const = 0;
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
	ui_container *parent_{nullptr};
};

class ui_container : public ui_element
{
      public:
	ui_container(std::string name, int x, int y, int width, int height) : ui_element(std::move(name), x, y, width, height)
	{
	}

	void add_child(std::unique_ptr<ui_element> child);

	void draw(int abs_x, int abs_y) const override;
	bool handle_event(const editor_event &ev, int abs_x, int abs_y) override;
	std::optional<std::string> get_value(const std::string &target_name) const override;
	std::optional<std::string> get_pressed_element_name() const override;

	bool has_overlay() const override;
	void draw_overlay(int abs_x, int abs_y) const override;

	void set_focus(bool focus) override;

	virtual bool focus_next();
	virtual bool focus_first();
	virtual bool focus_previous();
	virtual bool focus_last();
	virtual void child_got_selected(ui_element *child);
	virtual void set_focus_by_name(const std::string &child_name);

      protected:
	std::vector<std::unique_ptr<ui_element>> children_;
	ui_element *focused_child_{nullptr};
};

// Represents a single-line text input field.

#include "ui/components/ui_button.h"
#include "ui/components/ui_checkbox.h"
#include "ui/components/ui_fileselector.h"
#include "ui/components/ui_group_box.h"
#include "ui/components/ui_radio.h"
#include "ui/components/ui_text_label.h"
#include "ui/components/ui_textbox.h"
