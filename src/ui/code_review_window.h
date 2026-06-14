#pragma once
#include <memory>
#include <vector>
#include "codereview_manager.h"
#include "ui/components/ui_listbox.h"
#include "ui/window.h"

class code_review_window : public window
{
      public:
	code_review_window(int id, int x, int y, int width, int height, event_queue &global_queue, int focus_item_id = -1);
	~code_review_window() override = default;

	void draw_content(bool cursor_only = false) const override;
	void draw_border() const override;
	bool process_events() override;
	void set_cursor_position() const override;
	std::string get_displayed_title() const override;

	void refresh();
	void focus_item(int item_id);

      protected:
	bool update_viewport() const override;

      private:
	void populate_listbox();
	void go_to_source();
	void confirm_item();
	void dispute_item();
	void invalidate_item();
	void resolve_item();
	void verify_item();
	void edit_item();
	void add_comment();

	event_queue &global_queue_;
	std::unique_ptr<ui_listbox> listbox_;
	std::vector<review_item> current_items_;
	int detail_scroll_offset_{0};
	mutable int last_detail_scroll_offset_{0};
	int last_selected_index_{-1};
	int initial_focus_id_{-1};
};
