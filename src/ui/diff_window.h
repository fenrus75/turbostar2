#pragma once
#include <memory>
#include <string>
#include <vector>
#include "ui/components/ui_button.h"
#include "ui/window.h"

class diff_window : public window
{
      public:
	diff_window(int id, int x, int y, int width, int height, std::shared_ptr<document> doc, event_queue &global_queue);
	~diff_window() override = default;

	bool process_events() override;
	void draw_content(bool cursor_only = false) const override;
	std::string get_displayed_title() const override
	{
		return get_title();
	}
	void set_cursor_position() const override;

      protected:
	void on_resize(int width, int height) override;

      private:
	void update_diff();
	void go_prev();
	void go_next();
	void restore_state();

	size_t current_undo_step_{0};
	size_t max_undo_steps_{0};
	std::vector<std::string> diff_lines_;
	int scroll_y_{0};

	std::unique_ptr<ui_button> prev_button_;
	std::unique_ptr<ui_button> next_button_;
	std::unique_ptr<ui_button> restore_button_;
	event_queue &global_queue_;
};
