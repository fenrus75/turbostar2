#include "ui/components/ui_radio.h"
#include <algorithm>
#include <ctype.h>
#include <ncurses.h>
#include <sys/stat.h>
#include "fs_utils.h"

// --- ui_radio_choice ---

ui_radio_choice::ui_radio_choice(std::string name, int x, int y, const std::string &text, char hotkey, bool initial_state)
    : ui_element(std::move(name), x, y, text.length() + 4, 1), text_(text), hotkey_(hotkey), selected_(initial_state)
{
}

void ui_radio_choice::draw(int abs_x, int abs_y) const
{
	move(abs_y, abs_x);
	if (has_focus_)
		attrset(COLOR_PAIR(19));
	else
		attrset(COLOR_PAIR(17));

	addch('(');
	if (selected_) {
		addstr("•");
	} else {
		addch(' ');
	}
	addch(')');

	addch(' ');
	addstr(text_.c_str());

	if (hotkey_ != '\0') {
		size_t hk_pos = text_.find(hotkey_);
		if (hk_pos == std::string::npos)
			hk_pos = text_.find(tolower(hotkey_));
		if (hk_pos == std::string::npos)
			hk_pos = text_.find(toupper(hotkey_));

		if (hk_pos != std::string::npos) {
			attron(COLOR_PAIR(18));
			mvaddch(abs_y, abs_x + 4 + hk_pos, text_[hk_pos]);
		}
	}
	attrset(COLOR_PAIR(1));
}

bool ui_radio_choice::handle_event(const editor_event &ev, int abs_x, int abs_y)
{
	if (ev.type == event_type::key_press) {
		if ((ev.key_code == ' ' || ev.key_code == '\n' || ev.key_code == '\r' || ev.key_code == KEY_ENTER) && has_focus_) {
			selected_ = true;
			if (parent_)
				parent_->child_got_selected(this);
			return true;
		}

		if (hotkey_ != '\0' &&
		    (ev.key_code == -hotkey_ || ev.key_code == -tolower(hotkey_) || ev.key_code == -toupper(hotkey_) ||
		     (has_focus_ && (ev.key_code == hotkey_ || ev.key_code == tolower(hotkey_) || ev.key_code == toupper(hotkey_))))) {
			selected_ = true;
			if (parent_) {
				parent_->set_focus_by_name(name_);
				parent_->child_got_selected(this);
			}
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
			selected_ = true;
			if (parent_) {
				parent_->set_focus_by_name(name_);
				parent_->child_got_selected(this);
			}
			return true;
		}
	}
	return false;
}

std::optional<std::string> ui_radio_choice::get_value(const std::string &target_name) const
{
	if (name_ == target_name)
		return selected_ ? "true" : "false";
	return std::nullopt;
}

// --- ui_radiobutton_group ---

ui_radiobutton_group::ui_radiobutton_group(std::string name, int x, int y, int width, int height)
    : ui_container(std::move(name), x, y, width, height)
{
}

void ui_radiobutton_group::child_got_selected(ui_element *selected_child)
{
	for (auto &child : children_) {
		auto *radio = dynamic_cast<ui_radio_choice *>(child.get());
		if (radio) {
			if (radio != selected_child) {
				radio->set_selected(false);
			}
		}
	}
	// Also pass it up in case our parent is another group or needs to know
	if (parent_)
		parent_->child_got_selected(selected_child);
}

std::optional<std::string> ui_radiobutton_group::get_value(const std::string &target_name) const
{
	if (name_ == target_name) {
		// Return the name of the currently selected radio button
		for (const auto &child : children_) {
			auto *radio = dynamic_cast<ui_radio_choice *>(child.get());
			if (radio && radio->is_selected()) {
				return radio->name();
			}
		}
		return std::nullopt;
	}
	// Also allow children to be queried by their direct name
	for (const auto &child : children_) {
		auto val = child->get_value(target_name);
		if (val)
			return val;
	}
	return std::nullopt;
}
