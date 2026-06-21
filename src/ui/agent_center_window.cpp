#include "ui/agent_center_window.h"
#include <algorithm>
#include <ncurses.h>
#include "editor.h"
#include "event_logger.h"
#include "ui/components/ui_agent_tile.h"

agent_center_window::agent_center_window(int id, int x, int y, int width, int height, editor *ed)
    : window(id, x, y, width, height, "Agent Command Center"), editor_(ed)
{
	grid_ = std::make_unique<ui_grid_flow>("agent_grid", 1, 1, 2, 1, 0);
	sync_agent_tiles();
}

void agent_center_window::draw_content(bool cursor_only) const
{
	if (cursor_only) {
		return;
	}

	// Dynamic layout fit inside window content boundaries
	grid_->set_bounds(x_ + 1, y_ + 1, width_ - 2, height_ - 2);
	grid_->flow();
	grid_->draw(0, 0);
}

void agent_center_window::draw_border() const
{
	window::draw_border();
}

bool agent_center_window::process_events()
{
	bool needs_render = false;

	// Keep grid tiles in sync with editor active agent list
	sync_agent_tiles();

	// Cycle agent progress animations if active
	if (grid_->update_animation()) {
		needs_render = true;
		invalidate();
	}

	while (auto ev = get_queue().pop()) {
		if (grid_->handle_event(*ev, 0, 0)) {
			needs_render = true;
			invalidate();
		}
	}

	return needs_render;
}

void agent_center_window::set_cursor_position() const
{
	curs_set(0); // Hide standard typing cursor on the grid
}

std::string agent_center_window::get_displayed_title() const
{
	return "Agent Command Center";
}

void agent_center_window::sync_agent_tiles()
{
	if (!editor_) {
		return;
	}

	auto active_agents = editor_->get_all_active_agents();

	std::vector<int> current_ids;
	for (const auto &agent : active_agents) {
		current_ids.push_back(agent->get_id());
	}
	std::sort(current_ids.begin(), current_ids.end());

	if (current_ids == last_agent_ids_) {
		return;
	}

	grid_->clear_children();
	for (const auto &agent : active_agents) {
		auto tile = std::make_unique<ui_agent_tile>("tile_" + std::to_string(agent->get_id()), 0, 0, agent);
		grid_->add_child(std::move(tile));
	}

	last_agent_ids_ = current_ids;
	grid_->flow();
	invalidate();
}
