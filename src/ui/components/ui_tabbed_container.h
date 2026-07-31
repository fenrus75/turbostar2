#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "ui/components/ui_listbox.h"
#include "ui/ui_element.h"

struct tab_page_item {
	std::string id;
	std::string title;
	std::unique_ptr<ui_element> content;
};

// ui_tabbed_container is a composite container providing a tabbed layout.
// Left side: A vertical listbox/sidebar displaying category tab names.
// Right side: The active category content layout matching the selected tab.
//
// GUIDELINE FOR FUTURE AGENTS: Tab page titles should be kept concise so that
// the auto-calculated left sidebar width stays within a soft maximum of 20 characters.
class ui_tabbed_container : public ui_container
{
      public:
	ui_tabbed_container(std::string name, int x, int y, int width, int height);
	ui_tabbed_container(std::string name);

	// Tab Page Management
	void add_tab_page(const std::string &id, const std::string &title, std::unique_ptr<ui_element> page);
	size_t active_tab() const { return active_tab_; }
	size_t tab_count() const { return pages_.size(); }
	ui_element *get_tab_page(size_t index) const;
	ui_element *get_active_page() const;

	bool set_active_tab(size_t index);
	bool set_active_tab_by_id(const std::string &id);

	int sidebar_width() const { return sidebar_width_; }

	void set_tab_changed_callback(std::function<void(size_t index, const std::string &id)> cb)
	{
		tab_changed_cb_ = std::move(cb);
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

      private:
	void recalculate_sidebar_width();

	std::vector<tab_page_item> pages_;
	size_t active_tab_{0};
	int sidebar_width_{16};

	std::unique_ptr<ui_listbox> sidebar_list_;
	std::function<void(size_t, const std::string &)> tab_changed_cb_;
};
