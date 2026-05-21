#include "ui/ui_element.h"
#include <ncurses.h>
#include <ctype.h>
#include "event_logger.h"

void ui_container::add_child(std::unique_ptr<ui_element> child)
{
	child->set_parent(this);
	children_.push_back(std::move(child));
	
	// If it's the first child, give it focus by default
	if (!focused_child_ && !children_.empty()) {
		focused_child_ = children_.back().get();
		focused_child_->set_focus(true);
	}
}

void ui_container::draw(int abs_x, int abs_y) const
{
	for (const auto &child : children_) {
		child->draw(abs_x + child->x(), abs_y + child->y());
	}
}

bool ui_container::handle_event(const editor_event &ev, int abs_x, int abs_y)
{
	// 1. Give focused child first chance at keyboard events
	if (focused_child_ && ev.type == event_type::key_press) {
		if (focused_child_->handle_event(ev, abs_x + focused_child_->x(), abs_y + focused_child_->y())) {
			return true;
		}
	}
	
	// 1b. If it was a key press and the focused child didn't handle it, broadcast to others (for hotkeys)
	if (ev.type == event_type::key_press) {
		for (const auto &child : children_) {
			if (child.get() != focused_child_) {
				if (child->handle_event(ev, abs_x + child->x(), abs_y + child->y())) {
					return true;
				}
			}
		}
	}
	
	// 2. Mouse events: check all children based on coordinates
	if (ev.type == event_type::mouse_click) {
		for (const auto &child : children_) {
			int child_abs_x = abs_x + child->x();
			int child_abs_y = abs_y + child->y();
			
			// Even if the child doesn't contain the coord right now, we pass drag/up events down 
			// because they might be capturing the mouse. Let the child decide.
			// For down events, we strictly filter by bounding box.
			bool pass_event = false;
			if (ev.type == event_type::mouse_click) {
				pass_event = child->contains_coordinate(ev.mouse_x, ev.mouse_y, child_abs_x, child_abs_y);
			} else {
				// For now, pass all mouse up/drag to all children, let them filter if they don't care
				pass_event = true; 
			}

			if (pass_event) {
				if (child->handle_event(ev, child_abs_x, child_abs_y)) {
					// Auto-focus the clicked child
					if (ev.type == event_type::mouse_click && focused_child_ != child.get()) {
						if (focused_child_) focused_child_->set_focus(false);
						focused_child_ = child.get();
						focused_child_->set_focus(true);
					}
					return true;
				}
			}
		}
	}

	// 3. Handle Tab / Shift+Tab for navigation
	if (ev.type == event_type::key_press) {
		if (ev.key_code == '\t') {
			focus_next();
			return true;
		} else if (ev.key_code == KEY_BTAB) {
			focus_previous();
			return true;
		}
	}

	return false;
}

std::optional<std::string> ui_container::get_value(const std::string &target_name) const
{
	if (name_ == target_name) {
		return std::nullopt; // Containers usually don't have values themselves
	}
	for (const auto &child : children_) {
		auto val = child->get_value(target_name);
		if (val) return val;
	}
	return std::nullopt;
}

std::optional<std::string> ui_container::get_pressed_element_name() const
{
	for (const auto &child : children_) {
		auto val = child->get_pressed_element_name();
		if (val) return val;
	}
	return std::nullopt;
}

void ui_container::focus_next()
{
	if (children_.empty()) return;
	
	if (focused_child_) {
		focused_child_->set_focus(false);
		auto it = std::find_if(children_.begin(), children_.end(), 
			[this](const std::unique_ptr<ui_element>& p) { return p.get() == focused_child_; });
		
		if (it != children_.end() && std::next(it) != children_.end()) {
			focused_child_ = std::next(it)->get();
		} else {
			focused_child_ = children_.front().get(); // wrap
		}
	} else {
		focused_child_ = children_.front().get();
	}
	focused_child_->set_focus(true);
}

void ui_container::focus_previous()
{
	if (children_.empty()) return;
	
	if (focused_child_) {
		focused_child_->set_focus(false);
		auto it = std::find_if(children_.begin(), children_.end(), 
			[this](const std::unique_ptr<ui_element>& p) { return p.get() == focused_child_; });
		
		if (it != children_.begin()) {
			focused_child_ = std::prev(it)->get();
		} else {
			focused_child_ = children_.back().get(); // wrap
		}
	} else {
		focused_child_ = children_.back().get();
	}
	focused_child_->set_focus(true);
}

void ui_container::child_got_selected(ui_element *child)
{
	// Can be overridden by things like radio groups
	(void)child;
}

void ui_container::set_focus_by_name(const std::string &child_name)
{
	for (const auto &child : children_) {
		if (child->name() == child_name) {
			if (focused_child_) {
				focused_child_->set_focus(false);
			}
			focused_child_ = child.get();
			focused_child_->set_focus(true);
			return;
		}
	}
}

// --- ui_textbox ---

ui_textbox::ui_textbox(std::string name, int x, int y, int width, const std::string &initial_text, std::function<void(const std::string&)> on_submit)
    : ui_element(std::move(name), x, y, width, 1), buffer_(initial_text), cursor_pos_(initial_text.length()), on_submit_(std::move(on_submit))
{
}

void ui_textbox::draw(int abs_x, int abs_y) const
{
	attron(COLOR_PAIR(5));
	move(abs_y, abs_x);
	for (int i = 0; i < width_; ++i)
		addch(' ');
	
	std::string display_text = buffer_;
	int display_offset = 0;
	if (cursor_pos_ >= width_) {
		display_offset = cursor_pos_ - width_ + 1;
	}
	
	if (display_offset > 0 && display_offset < static_cast<int>(buffer_.length())) {
		display_text = buffer_.substr(display_offset);
	}
	if (static_cast<int>(display_text.length()) > width_) {
		display_text = display_text.substr(0, width_);
	}

	mvaddstr(abs_y, abs_x, display_text.c_str());

	if (has_focus_) {
		int cursor_x = abs_x + cursor_pos_ - display_offset;
		if (cursor_x >= abs_x && cursor_x < abs_x + width_) {
			attron(COLOR_PAIR(14)); // Black text on green bg for cursor
			char c = ' ';
			if (cursor_pos_ < static_cast<int>(buffer_.length())) {
				c = buffer_[cursor_pos_];
			}
			mvaddch(abs_y, cursor_x, c);
			attroff(COLOR_PAIR(14));
		}
	}
	
	attroff(COLOR_PAIR(5));
}

bool ui_textbox::handle_event(const editor_event &ev, int abs_x, int abs_y)
{
	if (ev.type == event_type::key_press) {
		if ((ev.key_code == '\n' || ev.key_code == '\r' || ev.key_code == KEY_ENTER) && has_focus_) {
			if (on_submit_) on_submit_(buffer_);
			return true;
		}

		if (has_focus_) {
			if (ev.key_code == KEY_RIGHT) {
				if (cursor_pos_ < static_cast<int>(buffer_.length())) cursor_pos_++;
				return true;
			}
			if (ev.key_code == KEY_LEFT) {
				if (cursor_pos_ > 0) cursor_pos_--;
				return true;
			}
			if (ev.key_code == KEY_HOME) {
				cursor_pos_ = 0;
				return true;
			}
			if (ev.key_code == KEY_END) {
				cursor_pos_ = buffer_.length();
				return true;
			}
			if (ev.key_code == KEY_BACKSPACE || ev.key_code == 127 || ev.key_code == 8) {
				if (cursor_pos_ > 0) {
					buffer_.erase(cursor_pos_ - 1, 1);
					cursor_pos_--;
				}
				return true;
			}
			if (ev.key_code == KEY_DC) { // Delete
				if (cursor_pos_ < static_cast<int>(buffer_.length())) {
					buffer_.erase(cursor_pos_, 1);
				}
				return true;
			}
			if (ev.key_code >= 32 && ev.key_code <= 126) {
				buffer_.insert(cursor_pos_, 1, static_cast<char>(ev.key_code));
				cursor_pos_++;
				return true;
			}
			
			if (ev.key_code == KEY_DOWN || ev.key_code == '\t') {
				if (parent_) parent_->focus_next();
				return true;
			}
			if (ev.key_code == KEY_UP || ev.key_code == KEY_BTAB) {
				if (parent_) parent_->focus_previous();
				return true;
			}
		}
	}
	
	if (ev.type == event_type::mouse_click) {
		if (contains_coordinate(ev.mouse_x, ev.mouse_y, abs_x, abs_y)) {
			int click_offset = ev.mouse_x - abs_x;
			
			int display_offset = 0;
			if (cursor_pos_ >= width_) {
				display_offset = cursor_pos_ - width_ + 1;
			}
			
			cursor_pos_ = std::min(static_cast<int>(buffer_.length()), click_offset + display_offset);
			return true;
		}
	}
	
	return false;
}

std::optional<std::string> ui_textbox::get_value(const std::string &target_name) const
{
	if (name_ == target_name) {
		return buffer_;
	}
	return std::nullopt;
}

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
	for (size_t j = 0; j < text_.length(); ++j) shadow_str += "▀";
	mvaddstr(abs_y + 1, abs_x + 1, shadow_str.c_str());
	
	if (has_focus_) attrset(COLOR_PAIR(10));
	else attrset(COLOR_PAIR(8));
	
	mvaddstr(abs_y, abs_x, text_.c_str());
	
	if (hotkey_ != '\0') {
		size_t hk_pos = text_.find(hotkey_);
		if (hk_pos != std::string::npos) {
			if (has_focus_) attron(COLOR_PAIR(12));
			else attron(COLOR_PAIR(11));
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
			if (on_click_) on_click_();
			return true;
		}
		
		if (hotkey_ != '\0' && (ev.key_code == hotkey_ || ev.key_code == tolower(hotkey_) || ev.key_code == toupper(hotkey_))) {
			set_pressed(true);
			if (on_click_) on_click_();
			return true;
		}

		if (has_focus_) {
			if (ev.key_code == KEY_RIGHT || ev.key_code == KEY_DOWN || ev.key_code == '\t') {
				if (parent_) parent_->focus_next();
				return true;
			}
			if (ev.key_code == KEY_LEFT || ev.key_code == KEY_UP || ev.key_code == KEY_BTAB) {
				if (parent_) parent_->focus_previous();
				return true;
			}
		}
	}
	
	if (ev.type == event_type::mouse_click) {
		if (contains_coordinate(ev.mouse_x, ev.mouse_y, abs_x, abs_y)) {
			// Actually we might want to trigger on mouse up, but down is fine for now
			set_pressed(true);
			if (on_click_) on_click_();
			return true;
		}
	}
	
	return false;
}

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
	if (checked_) addch('X');
	else addch(' ');
	addch(']');
	
	mvaddstr(abs_y, abs_x + 4, text_.c_str());
	
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
		
		if (hotkey_ != '\0' && (ev.key_code == hotkey_ || ev.key_code == tolower(hotkey_) || ev.key_code == toupper(hotkey_))) {
			checked_ = !checked_;
			if (parent_) parent_->set_focus_by_name(name_);
			return true;
		}

		if (has_focus_) {
			if (ev.key_code == KEY_RIGHT || ev.key_code == KEY_DOWN || ev.key_code == '\t') {
				if (parent_) parent_->focus_next();
				return true;
			}
			if (ev.key_code == KEY_LEFT || ev.key_code == KEY_UP || ev.key_code == KEY_BTAB) {
				if (parent_) parent_->focus_previous();
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

// --- ui_radio_choice ---

ui_radio_choice::ui_radio_choice(std::string name, int x, int y, const std::string &text, char hotkey, bool initial_state)
    : ui_element(std::move(name), x, y, text.length() + 4, 1), text_(text), hotkey_(hotkey), selected_(initial_state)
{
}

void ui_radio_choice::draw(int abs_x, int abs_y) const
{
	move(abs_y, abs_x);
	if (has_focus_) attrset(COLOR_PAIR(19));
	else attrset(COLOR_PAIR(17));
	
	addch('(');
	if (selected_) addstr("•");
	else addch(' ');
	
	if (has_focus_) attrset(COLOR_PAIR(19));
	else attrset(COLOR_PAIR(17));
	addch(')');
	
	mvaddstr(abs_y, abs_x + 4, text_.c_str());
	
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

bool ui_radio_choice::handle_event(const editor_event &ev, int abs_x, int abs_y)
{
	if (ev.type == event_type::key_press) {
		if ((ev.key_code == ' ' || ev.key_code == '\n' || ev.key_code == '\r' || ev.key_code == KEY_ENTER) && has_focus_) {
			selected_ = true;
			if (parent_) parent_->child_got_selected(this);
			return true;
		}
		
		if (hotkey_ != '\0' && (ev.key_code == hotkey_ || ev.key_code == tolower(hotkey_) || ev.key_code == toupper(hotkey_))) {
			selected_ = true;
			if (parent_) {
				parent_->set_focus_by_name(name_);
				parent_->child_got_selected(this);
			}
			return true;
		}

		if (has_focus_) {
			if (ev.key_code == KEY_RIGHT || ev.key_code == KEY_DOWN || ev.key_code == '\t') {
				if (parent_) parent_->focus_next();
				return true;
			}
			if (ev.key_code == KEY_LEFT || ev.key_code == KEY_UP || ev.key_code == KEY_BTAB) {
				if (parent_) parent_->focus_previous();
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
	if (name_ == target_name) return selected_ ? "true" : "false";
	return std::nullopt;
}

// --- ui_radiobutton_group ---

ui_radiobutton_group::ui_radiobutton_group(std::string name, int x, int y, int width, int height)
    : ui_container(std::move(name), x, y, width, height)
{
}

void ui_radiobutton_group::child_got_selected(ui_element *selected_child)
{
	for (auto& child : children_) {
		auto* radio = dynamic_cast<ui_radio_choice*>(child.get());
		if (radio) {
			if (radio != selected_child) {
				radio->set_selected(false);
			}
		}
	}
	// Also pass it up in case our parent is another group or needs to know
	if (parent_) parent_->child_got_selected(selected_child);
}

std::optional<std::string> ui_radiobutton_group::get_value(const std::string &target_name) const
{
	if (name_ == target_name) {
		// Return the name of the currently selected radio button
		for (const auto& child : children_) {
			auto* radio = dynamic_cast<ui_radio_choice*>(child.get());
			if (radio && radio->is_selected()) {
				return radio->name();
			}
		}
		return std::nullopt;
	}
	// Fallback to standard container recursive search
	return ui_container::get_value(target_name);
}
