#pragma once

#include <memory>
#include <vector>
#include "ui/components/ui_grid_flow.h"
#include "ui/window.h"

class editor;

class agent_center_window : public window
{
      public:
	agent_center_window(int id, int x, int y, int width, int height, editor *ed);
	~agent_center_window() override = default;

	void draw_content(bool cursor_only = false) const override;
	bool process_events() override;
	void set_cursor_position() const override;
	std::string get_displayed_title() const override;

      private:
	void sync_agent_tiles();

	editor *editor_{nullptr};
	std::unique_ptr<ui_grid_flow> grid_;
	std::vector<int> last_agent_ids_;
};
