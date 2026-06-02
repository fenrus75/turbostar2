#include "ui/components/ui_button.h"
#include <algorithm>
#include <ctype.h>
#include <ncurses.h>
#include <sys/stat.h>
#include "fs_utils.h"

// --- ui_button ---

ui_button::ui_button(std::string name, int x, int y, const std::string &text, char hotkey, std::function<void()> on_click)
    : ui_element(std::move(name), x, y, text.length(), 1), text_(text), hotkey_(hotkey), on_click_(std::move(on_click))
{
}

void ui_button::draw(int abs_x, int abs_y) const
{
	attron(COLOR_PAIR(1));
	mvaddstr(abs_y, abs_x + text_.length(), "▄");
	std::string shadow_str;
	for (size_t j = 0; j < text_.length(); ++j)
		shadow_str += "▀";
	mvaddstr(abs_y + 1, abs_x + 1, shadow_str.c_str());

	if (has_focus_)
		attrset(COLOR_PAIR(40));
	else
		attrset(COLOR_PAIR(10));

	mvaddstr(abs_y, abs_x, text_.c_str());

	if (hotkey_ != '\0') {
		size_t hk_pos = text_.find(hotkey_);
		if (hk_pos != std::string::npos) {
			attron(COLOR_PAIR(40));
			mvaddch(abs_y, abs_x + hk_pos, text_[hk_pos]);
		}
	}
	attrset(COLOR_PAIR(1));
}

bool ui_button::handle_event(const editor_event &ev, int abs_x, int abs_y)
{
	if (ev.type == event_type::key_press) {
		if ((ev.key_code == '\n' || ev.key_code == '\r') && has_focus_) {
			set_pressed(true);
			if (on_click_)
				on_click_();
			return true;
		}

		if (hotkey_ != '\0' &&
		    (ev.key_code == -hotkey_ || ev.key_code == -tolower(hotkey_) || ev.key_code == -toupper(hotkey_) ||
		     (has_focus_ && (ev.key_code == hotkey_ || ev.key_code == tolower(hotkey_) || ev.key_code == toupper(hotkey_))))) {
			set_pressed(true);
			if (on_click_)
				on_click_();
			return true;
		}

		if (has_focus_) {
			if (ev.key_code == KEY_RIGHT || ev.key_code == KEY_DOWN || ev.key_code == '\t') {
				ui_element *p = parent_;
				while (p) {
					if (p->focus_next())
						break;
					p = p->parent();
				}
				return true;
			}
			if (ev.key_code == KEY_LEFT || ev.key_code == KEY_UP || ev.key_code == KEY_BTAB) {
				ui_element *p = parent_;
				while (p) {
					if (p->focus_previous())
						break;
					p = p->parent();
				}
				return true;
			}
		}
	}

	if (ev.type == event_type::mouse_click) {
		if (contains_coordinate(ev.mouse_x, ev.mouse_y, abs_x, abs_y)) {
			// Actually we might want to trigger on mouse up, but down is fine for now
			set_pressed(true);
			if (on_click_)
				on_click_();
			return true;
		}
	}

	return false;
}
