#include "ui/code_review_window.h"
#include <algorithm>
#include <cctype>
#include <ctime>
#include <format>
#include <ncurses.h>
#include <sstream>
#include "editor.h"
#include "project_manager.h"
#include "ui/dialog_factories.h"
#include "ui/window.h"

code_review_window::code_review_window(int id, int x, int y, int width, int height, event_queue &global_queue, int focus_item_id)
    : window(id, x, y, width, height, "Code Reviews"), global_queue_(global_queue), initial_focus_id_(focus_item_id)
{
	set_background_color_pair(2); // Use standard window background

	int left_width = std::max(30, static_cast<int>(width_ * 0.35));
	int content_height = height_ - 2;

	listbox_ = std::make_unique<ui_listbox>(
	    "codereviews", 0, 0, left_width, content_height,
	    [this](int new_index) {
		    if (new_index != last_selected_index_) {
			    detail_scroll_offset_ = 0;
			    last_selected_index_ = new_index;
			    invalidate();
		    }
	    },
	    [this](int /*index*/) { go_to_source(); });

	populate_listbox();

	if (initial_focus_id_ != -1) {
		focus_item(initial_focus_id_);
	}
}

void code_review_window::populate_listbox()
{
	current_items_ = codereview_manager::get_instance().list_code_review_items("", "", true);
	std::vector<std::string> display_items;

	for (const auto &item : current_items_) {
		std::string state_str = item.state;
		for (auto &c : state_str) {
			c = std::toupper(static_cast<unsigned char>(c));
		}
		display_items.push_back(std::format("#{} [{}] {}: {}", item.id, item.severity, state_str, item.summary));
	}

	listbox_->set_items(display_items);
}

void code_review_window::draw_content(bool /*cursor_only*/) const
{
	if (!is_visible())
		return;

	int left_width = std::max(30, static_cast<int>(width_ * 0.35));
	int right_width = width_ - 2 - left_width - 1;

	int current_y = y_ + 1;
	int start_x = x_ + 1;
	int content_height = height_ - 2;

	attron(COLOR_PAIR(get_background_color_pair()));

	// Clear content area
	for (int i = 1; i < height_ - 1; ++i) {
		move(y_ + i, x_ + 1);
		for (int j = 0; j < width_ - 2; ++j)
			addch(' ');
	}

	if (listbox_) {
		listbox_->set_bounds(start_x, current_y, left_width, content_height);
		listbox_->set_focus(is_active());
		listbox_->draw(listbox_->x(), listbox_->y());
	}

	// Draw vertical separator
	int sep_x = start_x + left_width;
	for (int i = 0; i < content_height; ++i) {
		mvaddch(current_y + i, sep_x, ACS_VLINE);
	}

	int details_x = sep_x + 1;
	int selected_idx = listbox_ ? listbox_->get_selected_index() : -1;
	if (selected_idx >= 0 && selected_idx < (int)current_items_.size()) {
		const auto &item = current_items_[selected_idx];

		std::string date_str = "N/A";
		if (item.datestamp > 0) {
			std::time_t t = static_cast<std::time_t>(item.datestamp);
			char buf[64];
			if (std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&t))) {
				date_str = buf;
			}
		}

		auto wrap_text = [](const std::string &text, int max_w) -> std::vector<std::string> {
			std::vector<std::string> res;
			std::stringstream ss(text);
			std::string line;
			while (std::getline(ss, line)) {
				if (line.empty()) {
					res.push_back("");
					continue;
				}
				size_t start = 0;
				while (start < line.length()) {
					if (line.length() - start <= static_cast<size_t>(max_w)) {
						res.push_back(line.substr(start));
						break;
					}
					size_t last_space = line.find_last_of(" \t", start + max_w);
					if (last_space == std::string::npos || last_space <= start) {
						res.push_back(line.substr(start, max_w));
						start += max_w;
					} else {
						res.push_back(line.substr(start, last_space - start));
						start = last_space + 1;
					}
				}
			}
			return res;
		};

		std::vector<std::string> lines;
		lines.push_back(std::format("ID:       #{}", item.id));
		lines.push_back(std::format("Severity: {}", item.severity));
		lines.push_back(std::format("State:    {}", item.state));
		lines.push_back(std::format("File:     {}:{}", item.filename, item.line_number));
		// Ensure the line content doesn't contain raw newlines or carriage returns,
		// as they would interfere with ncurses cursor positioning and corrupt the layout.
		// All newlines and carriage returns are replaced with single spaces.
		std::string clean_line_content = item.line_content;
		std::replace(clean_line_content.begin(), clean_line_content.end(), '\n', ' ');
		std::replace(clean_line_content.begin(), clean_line_content.end(), '\r', ' ');
		
		// Wrap the line content display to fit the pane width
		auto line_text_wrapped = wrap_text(std::format("Line text: {}", clean_line_content), right_width - 2);
		lines.insert(lines.end(), line_text_wrapped.begin(), line_text_wrapped.end());

		lines.push_back(std::format("Date:     {}", date_str));
		lines.push_back(std::format("Commit:   {}", item.git_hash));
		if (!item.resolved_in_commit.empty()) {
			lines.push_back(std::format("Resolved in Commit: {}", item.resolved_in_commit));
		}
		lines.push_back("------------------------------------------------------------------");
		lines.push_back("Summary: " + item.summary);
		lines.push_back("------------------------------------------------------------------");
		lines.push_back("Description:");

		auto desc_wrapped = wrap_text(item.description, right_width - 2);
		lines.insert(lines.end(), desc_wrapped.begin(), desc_wrapped.end());

		lines.push_back("------------------------------------------------------------------");
		lines.push_back("Proposed Fix:");
		auto fix_wrapped = wrap_text(item.proposed_fix, right_width - 2);
		lines.insert(lines.end(), fix_wrapped.begin(), fix_wrapped.end());

		int details_height = content_height;
		for (int i = 0; i < details_height; ++i) {
			int line_idx = detail_scroll_offset_ + i;
			if (line_idx < (int)lines.size()) {
				std::string display_line = lines[line_idx];
				if (display_line.length() > static_cast<size_t>(right_width)) {
					display_line = display_line.substr(0, right_width);
				}
				mvprintw(current_y + i, details_x, "%s", display_line.c_str());
			}
		}
	} else {
		mvprintw(current_y, details_x, " No code review items found.");
	}

	attroff(COLOR_PAIR(get_background_color_pair()));
}

void code_review_window::draw_border() const
{
	::window::draw_border();

	attrset(COLOR_PAIR(is_active() ? 5 : 38));
	mvaddstr(y_ + height_ - 1, x_ + 2, "[G] Go to Src");
	addstr("  [C] Confirm");
	addstr("  [D] Dispute");
	addstr("  [I] Invalidate");
	addstr("  [R] Resolve");
	addstr("  [V] Verify");
	addstr("  [E] Edit");
	addstr("  [A] Comment");
	addstr("  [P] Re-process");
}

bool code_review_window::process_events()
{
	bool needs_render = false;

	while (auto ev = get_window_queue().pop()) {
		if (ev->type == event_type::key_press && is_active()) {
			int key = ev->key_code;

			if (key == 21) { // Ctrl-U (Page up)
				detail_scroll_offset_ -= 10;
				if (detail_scroll_offset_ < 0)
					detail_scroll_offset_ = 0;
				needs_render = true;
			} else if (key == 22) { // Ctrl-V (Page down)
				detail_scroll_offset_ += 10;
				needs_render = true;
			} else if (key == 'g' || key == 'G' || key == 10) { // Enter or G
				go_to_source();
				needs_render = true;
			} else if (key == 'c' || key == 'C') {
				confirm_item();
				needs_render = true;
			} else if (key == 'd' || key == 'D') {
				dispute_item();
				needs_render = true;
			} else if (key == 'i' || key == 'I') {
				invalidate_item();
				needs_render = true;
			} else if (key == 'r' || key == 'R') {
				resolve_item();
				needs_render = true;
			} else if (key == 'v' || key == 'V') {
				verify_item();
				needs_render = true;
			} else if (key == 'e' || key == 'E') {
				edit_item();
				needs_render = true;
			} else if (key == 'a' || key == 'A') {
				add_comment();
				needs_render = true;
			} else if (key == 'p' || key == 'P') {
				reprocess_item();
				needs_render = true;
			} else if (key == 's' || key == 'S') {
				int selected_idx = listbox_ ? listbox_->get_selected_index() : -1;
				if (selected_idx >= 0 && selected_idx < (int)current_items_.size()) {
					editor_event action_ev;
					action_ev.type = event_type::codereview_action;
					action_ev.key_code = current_items_[selected_idx].id;
					action_ev.payload = "state";
					global_queue_.push(action_ev);
				}
				needs_render = true;
			} else if (key == 'y' || key == 'Y') {
				int selected_idx = listbox_ ? listbox_->get_selected_index() : -1;
				if (selected_idx >= 0 && selected_idx < (int)current_items_.size()) {
					editor_event action_ev;
					action_ev.type = event_type::codereview_action;
					action_ev.key_code = current_items_[selected_idx].id;
					action_ev.payload = "severity";
					global_queue_.push(action_ev);
				}
				needs_render = true;
			} else if (listbox_ && listbox_->handle_event(*ev, 0, 0)) {
				needs_render = true;
			}
		} else if (ev->type == event_type::mouse_click || ev->type == event_type::mouse_scroll_up ||
			   ev->type == event_type::mouse_scroll_down) {
			if (ev->type == event_type::mouse_click && ev->mouse_y == y_ + height_ - 1) {
				int click_x = ev->mouse_x - x_;
				int selected_idx = listbox_ ? listbox_->get_selected_index() : -1;
				if (selected_idx >= 0 && selected_idx < (int)current_items_.size()) {
					// Check click coordinates relative to actions list on bottom border
					if (click_x >= 2 && click_x < 15) {
						go_to_source();
						needs_render = true;
					} else if (click_x >= 17 && click_x < 28) {
						confirm_item();
						needs_render = true;
					} else if (click_x >= 30 && click_x < 41) {
						dispute_item();
						needs_render = true;
					} else if (click_x >= 43 && click_x < 57) {
						invalidate_item();
						needs_render = true;
					} else if (click_x >= 59 && click_x < 70) {
						resolve_item();
						needs_render = true;
					} else if (click_x >= 72 && click_x < 82) {
						verify_item();
						needs_render = true;
					} else if (click_x >= 84 && click_x < 92) {
						edit_item();
						needs_render = true;
					} else if (click_x >= 94 && click_x < 105) {
						add_comment();
						needs_render = true;
					} else if (click_x >= 107 && click_x < 123) {
						reprocess_item();
						needs_render = true;
					}
				}
			} else if (listbox_ && listbox_->handle_event(*ev, 0, 0)) {
				needs_render = true;
			}
		}
	}

	if (needs_render)
		invalidate();
	return needs_render;
}

void code_review_window::set_cursor_position() const
{
	if (is_active() && listbox_) {
		listbox_->set_cursor_position(listbox_->x(), listbox_->y());
	}
}

void code_review_window::go_to_source()
{
	int selected_idx = listbox_ ? listbox_->get_selected_index() : -1;
	if (selected_idx >= 0 && selected_idx < (int)current_items_.size()) {
		const auto &item = current_items_[selected_idx];
		if (!item.filename.empty()) {
			std::string proj_root = project_manager::get_instance().get_project_root();
			std::filesystem::path p(item.filename);
			if (!p.is_absolute()) {
				p = std::filesystem::path(proj_root) / p;
			}
			std::string abs_file = p.lexically_normal().string();
			editor_event open_ev;
			open_ev.type = event_type::open_file;
			open_ev.payload = abs_file + ":" + std::to_string(std::max(1, item.line_number));
			global_queue_.push(open_ev);
		}
	}
}

void code_review_window::confirm_item()
{
	int selected_idx = listbox_ ? listbox_->get_selected_index() : -1;
	if (selected_idx >= 0 && selected_idx < (int)current_items_.size()) {
		int id = current_items_[selected_idx].id;
		if (codereview_manager::get_instance().confirm_code_review_item(id)) {
			codereview_manager::get_instance().save_project();
			editor_event update_ev;
			update_ev.type = event_type::codereview_updated;
			update_ev.key_code = id;
			global_queue_.push(update_ev);
		}
	}
}

void code_review_window::dispute_item()
{
	int selected_idx = listbox_ ? listbox_->get_selected_index() : -1;
	if (selected_idx >= 0 && selected_idx < (int)current_items_.size()) {
		int id = current_items_[selected_idx].id;
		if (codereview_manager::get_instance().update_code_review_item(id, "disputed", std::nullopt, std::nullopt, std::nullopt)) {
			codereview_manager::get_instance().save_project();
			editor_event update_ev;
			update_ev.type = event_type::codereview_updated;
			update_ev.key_code = id;
			global_queue_.push(update_ev);
		}
	}
}

void code_review_window::invalidate_item()
{
	int selected_idx = listbox_ ? listbox_->get_selected_index() : -1;
	if (selected_idx >= 0 && selected_idx < (int)current_items_.size()) {
		int id = current_items_[selected_idx].id;
		if (codereview_manager::get_instance().update_code_review_item(id, "invalid", std::nullopt, std::nullopt, std::nullopt)) {
			codereview_manager::get_instance().save_project();
			editor_event update_ev;
			update_ev.type = event_type::codereview_updated;
			update_ev.key_code = id;
			global_queue_.push(update_ev);
		}
	}
}

void code_review_window::resolve_item()
{
	int selected_idx = listbox_ ? listbox_->get_selected_index() : -1;
	if (selected_idx >= 0 && selected_idx < (int)current_items_.size()) {
		int id = current_items_[selected_idx].id;
		if (codereview_manager::get_instance().resolve_code_review_item(id, "")) {
			codereview_manager::get_instance().save_project();
			editor_event update_ev;
			update_ev.type = event_type::codereview_updated;
			update_ev.key_code = id;
			global_queue_.push(update_ev);
		}
	}
}

void code_review_window::verify_item()
{
	int selected_idx = listbox_ ? listbox_->get_selected_index() : -1;
	if (selected_idx >= 0 && selected_idx < (int)current_items_.size()) {
		int id = current_items_[selected_idx].id;
		if (codereview_manager::get_instance().update_code_review_item(id, "verified-fixed", std::nullopt, std::nullopt, std::nullopt)) {
			codereview_manager::get_instance().save_project();
			editor_event update_ev;
			update_ev.type = event_type::codereview_updated;
			update_ev.key_code = id;
			global_queue_.push(update_ev);
		}
	}
}

void code_review_window::edit_item()
{
	int selected_idx = listbox_ ? listbox_->get_selected_index() : -1;
	if (selected_idx >= 0 && selected_idx < (int)current_items_.size()) {
		int id = current_items_[selected_idx].id;
		editor_event action_ev;
		action_ev.type = event_type::codereview_action;
		action_ev.key_code = id;
		action_ev.payload = "edit";
		global_queue_.push(action_ev);
	}
}

void code_review_window::add_comment()
{
	int selected_idx = listbox_ ? listbox_->get_selected_index() : -1;
	if (selected_idx >= 0 && selected_idx < (int)current_items_.size()) {
		int id = current_items_[selected_idx].id;
		editor_event action_ev;
		action_ev.type = event_type::codereview_action;
		action_ev.key_code = id;
		action_ev.payload = "comment";
		global_queue_.push(action_ev);
	}
}

std::string code_review_window::get_displayed_title() const
{
	return "Code Reviews";
}

bool code_review_window::update_viewport() const
{
	bool changed = (detail_scroll_offset_ != last_detail_scroll_offset_);
	last_detail_scroll_offset_ = detail_scroll_offset_;
	return changed;
}

void code_review_window::refresh()
{
	int prev_id = -1;
	int selected_idx = listbox_ ? listbox_->get_selected_index() : -1;
	if (selected_idx >= 0 && selected_idx < (int)current_items_.size()) {
		prev_id = current_items_[selected_idx].id;
	}

	populate_listbox();

	if (prev_id != -1) {
		focus_item(prev_id);
	}
}

void code_review_window::focus_item(int item_id)
{
	for (size_t i = 0; i < current_items_.size(); ++i) {
		if (current_items_[i].id == item_id) {
			listbox_->set_selected_index(static_cast<int>(i));
			last_selected_index_ = static_cast<int>(i);
			detail_scroll_offset_ = 0;
			invalidate();
			break;
		}
	}
}

void code_review_window::reprocess_item()
{
	int selected_idx = listbox_ ? listbox_->get_selected_index() : -1;
	if (selected_idx >= 0 && selected_idx < (int)current_items_.size()) {
		int id = current_items_[selected_idx].id;
		editor_event action_ev;
		action_ev.type = event_type::codereview_action;
		action_ev.key_code = id;
		action_ev.payload = "reprocess";
		global_queue_.push(action_ev);
	}
}
