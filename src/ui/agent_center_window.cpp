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

	// Clear background of content area to Dark Blue (Pair 3)
	int bg_pair = 3;
	attron(COLOR_PAIR(bg_pair));
	for (int dy = 0; dy < height_ - 2; ++dy) {
		mvprintw(y_ + 1 + dy, x_ + 1, "%*s", width_ - 2, "");
	}
	attroff(COLOR_PAIR(bg_pair));

	// Dynamic layout fit inside window content boundaries
	grid_->set_bounds(x_ + 1, y_ + 1, width_ - 2, height_ - 2);
	grid_->flow();
	grid_->draw(x_, y_);
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
		// 1. Intercept keyboard arrow keys for 2D grid navigation
		if (ev->type == event_type::key_press) {
			const auto &children = grid_->children();
			if (!children.empty()) {
				ui_element *current = grid_->focused_child();
				int idx = -1;
				if (current) {
					for (size_t i = 0; i < children.size(); ++i) {
						if (children[i].get() == current) {
							idx = static_cast<int>(i);
							break;
						}
					}
				}

				int target_idx = -1;
				int key = ev->key_code;
				int cell_width = 22;
				int h_spacer = grid_->h_spacer();
				int x_offset = grid_->x_offset();
				int cols = 1;
				int avail_w = grid_->width() - 2 * x_offset;
				if (avail_w > cell_width) {
					cols = (avail_w + h_spacer) / (cell_width + h_spacer);
				}
				if (cols < 1) {
					cols = 1;
				}

				if (key == KEY_LEFT) {
					if (idx > 0) {
						target_idx = idx - 1;
					}
				} else if (key == KEY_RIGHT) {
					if (idx != -1 && idx + 1 < (int)children.size()) {
						target_idx = idx + 1;
					}
				} else if (key == KEY_UP) {
					if (idx != -1 && idx - cols >= 0) {
						target_idx = idx - cols;
					}
				} else if (key == KEY_DOWN) {
					if (idx != -1 && idx + cols < (int)children.size()) {
						target_idx = idx + cols;
					}
				}

				if (target_idx != -1) {
					if (current) {
						current->set_focus(false);
					}
					ui_element *target_child = children[target_idx].get();
					grid_->set_focused_child(target_child);
					target_child->set_focus(true);
					needs_render = true;
					invalidate();
					continue;
				}
			}
		}

		// 2. Intercept Enter or Space keys to open focused agent's window
		if (ev->type == event_type::key_press &&
		    (ev->key_code == '\n' || ev->key_code == '\r' || ev->key_code == KEY_ENTER || ev->key_code == ' ')) {
			ui_element *focused = grid_->focused_child();
			if (focused) {
				if (auto *tile = dynamic_cast<ui_agent_tile *>(focused)) {
					if (auto agent = tile->get_agent()) {
						editor_event open_ev;
						open_ev.type = event_type::open_subagent;
						open_ev.key_code = agent->get_id();
						editor_->dispatch(open_ev);
						needs_render = true;
						invalidate();
						continue;
					}
				}
			}
		}

		if (grid_->handle_event(*ev, x_, y_)) {
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

	// Track the ID of the currently focused agent to restore it post-sync
	int last_focused_id = -1;
	if (grid_->focused_child()) {
		if (auto *tile = dynamic_cast<ui_agent_tile *>(grid_->focused_child())) {
			if (tile->get_agent()) {
				last_focused_id = tile->get_agent()->get_id();
			}
		}
	}

	grid_->clear_children();
	for (const auto &agent : active_agents) {
		auto tile = std::make_unique<ui_agent_tile>("tile_" + std::to_string(agent->get_id()), 0, 0, agent);
		grid_->add_child(std::move(tile));
	}

	last_agent_ids_ = current_ids;
	grid_->flow();

	// Restore focus to previous agent or fallback to the first active one
	ui_element *to_focus = nullptr;
	if (last_focused_id != -1) {
		for (const auto &child : grid_->children()) {
			if (auto *tile = dynamic_cast<ui_agent_tile *>(child.get())) {
				if (tile->get_agent() && tile->get_agent()->get_id() == last_focused_id) {
					to_focus = child.get();
					break;
				}
			}
		}
	}
	if (!to_focus && !grid_->children().empty()) {
		to_focus = grid_->children().front().get();
	}

	if (to_focus) {
		grid_->set_focused_child(to_focus);
		to_focus->set_focus(true);
	}

	invalidate();
}
