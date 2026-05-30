#pragma once
#include <memory>
#include <vector>
#include "crashdump_manager.h"
#include "ui/components/ui_listbox.h"
#include "ui/window.h"

class crashdump_window : public window
{
      public:
	crashdump_window(int id, int x, int y, int width, int height, event_queue &global_queue);
	~crashdump_window() override = default;

	void draw_content() const override;
	void draw_border() const override;
	bool process_events() override;
	void set_cursor_position() const override;
	std::string get_displayed_title() const override;

      private:
	void populate_listbox();
	void go_to_source();
	std::optional<std::pair<std::string, int>> get_crash_location(const crashdump_info &dump) const;

	event_queue &global_queue_;
	std::unique_ptr<ui_listbox> listbox_;
	std::vector<crashdump_info> current_dumps_;
	int detail_scroll_offset_{0};
	int last_selected_index_{-1};
};