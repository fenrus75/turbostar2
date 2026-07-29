#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "ui/components/ui_button.h"
#include "ui/components/ui_buttons_horizontal.h"
#include "ui/ui_element.h"

// ui_paged_container is a specialized composite container for multi-step
// wizard workflows. It manages a sequence of child page elements, rendering and
// routing events exclusively to the active page layout while providing a standard
// bottom navigation bar (<< Back, Cancel, Next >> / Finish).
class ui_paged_container : public ui_container
{
      public:
	ui_paged_container(std::string name, int x, int y, int width, int height);
	ui_paged_container(std::string name);

	// Page Management
	void add_page(std::unique_ptr<ui_element> page);
	size_t current_page() const { return current_page_; }
	size_t page_count() const { return pages_.size(); }
	ui_element *get_page(size_t index) const;

	bool set_current_page(size_t page_index);
	bool next_page();
	bool previous_page();

	// Virtual hooks for subclassing
	virtual bool on_validate_page(size_t page_index, std::string &out_error);
	virtual void on_page_entered(size_t page_index);

	// Callback setters for inline lambdas
	void set_validate_page_callback(std::function<bool(size_t page, std::string &out_error)> cb)
	{
		validate_cb_ = std::move(cb);
	}
	void set_page_entered_callback(std::function<void(size_t page)> cb)
	{
		page_entered_cb_ = std::move(cb);
	}

	// ui_container overrides
	void draw(int abs_x, int abs_y) const override;
	bool handle_event(const editor_event &ev, int abs_x, int abs_y) override;
	bool flow() override;
	int natural_width() const override;
	int natural_height() const override;

	bool focus_first() override;
	bool focus_last() override;
	bool focus_next() override;
	bool focus_previous() override;
	std::vector<ui_element *> get_focusable_elements() override;
	std::optional<std::string> get_value(const std::string &target_name) const override;
	std::optional<std::string> get_pressed_element_name() const override;

      private:
	void init_button_bar();
	void update_button_states();

	std::vector<std::unique_ptr<ui_element>> pages_;
	size_t current_page_{0};

	std::unique_ptr<ui_buttons_horizontal> button_bar_;
	ui_button *back_btn_{nullptr};
	ui_button *cancel_btn_{nullptr};
	ui_button *next_btn_{nullptr};

	std::function<bool(size_t, std::string &)> validate_cb_;
	std::function<void(size_t)> page_entered_cb_;
};
