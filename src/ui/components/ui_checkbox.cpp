#include "ui/components/ui_checkbox.h"
#include <ncurses.h>
#include <ctype.h>
#include "fs_utils.h"
#include <sys/stat.h>
#include <algorithm>

// --- ui_checkbox ---

ui_checkbox::ui_checkbox(std::string name, int x, int y, const std::string &text, char hotkey, bool initial_state)
    : ui_element(std::move(name), x, y, text.length() + 4, 1), text_(text), hotkey_(hotkey), checked_(initial_state)
{
}

void ui_checkbox::draw(int abs_x, int abs_y) const
{
	move(abs_y, abs_x);
	if (has_focus_) attrset(COLOR_PAIR(19));
	else attrset(COLOR_PAIR(17));
	
	addch('[');
	if (checked_) {
		addch('X');
	} else {
		addch(' ');
	}
	addch(']');
	
	addch(' ');
	addstr(text_.c_str());
	
	if (hotkey_ != '\0') {
		size_t hk_pos = text_.find(hotkey_);
		if (hk_pos == std::string::npos) hk_pos = text_.find(tolower(hotkey_));
		if (hk_pos == std::string::npos) hk_pos = text_.find(toupper(hotkey_));
		
		if (hk_pos != std::string::npos) {
			attron(COLOR_PAIR(18));
			mvaddch(abs_y, abs_x + 4 + hk_pos, text_[hk_pos]);
		}
	}
	attrset(COLOR_PAIR(1));
}

bool ui_checkbox::handle_event(const editor_event &ev, int abs_x, int abs_y)
{
	if (ev.type == event_type::key_press) {
		if ((ev.key_code == ' ' || ev.key_code == '\n' || ev.key_code == '\r' || ev.key_code == KEY_ENTER) && has_focus_) {
			checked_ = !checked_;
			return true;
		}
		
		if (hotkey_ != '\0' && (ev.key_code == -hotkey_ || ev.key_code == -tolower(hotkey_) || ev.key_code == -toupper(hotkey_) || (has_focus_ && (ev.key_code == hotkey_ || ev.key_code == tolower(hotkey_) || ev.key_code == toupper(hotkey_))))) {
			checked_ = !checked_;
			if (parent_) parent_->set_focus_by_name(name_);
			return true;
		}

		if (has_focus_) {
			if (ev.key_code == KEY_RIGHT || ev.key_code == KEY_DOWN || ev.key_code == '\t') {
				ui_element* p = parent_;
				while (p) { if (p->focus_next()) break; p = p->parent(); }
				return true;
			}
			if (ev.key_code == KEY_LEFT || ev.key_code == KEY_UP || ev.key_code == KEY_BTAB) {
				ui_element* p = parent_;
				while (p) { if (p->focus_previous()) break; p = p->parent(); }
				return true;
			}
		}
	}
	
	if (ev.type == event_type::mouse_click) {
		if (contains_coordinate(ev.mouse_x, ev.mouse_y, abs_x, abs_y)) {
			checked_ = !checked_;
			if (parent_) parent_->set_focus_by_name(name_);
			return true;
		}
	}
	return false;
}

std::optional<std::string> ui_checkbox::get_value(const std::string &target_name) const
{
	if (name_ == target_name) return checked_ ? "true" : "false";
	return std::nullopt;
}
