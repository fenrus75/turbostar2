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
		// Only propagate true if the container itself has focus
		focused_child_->set_focus(has_focus_);
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

void ui_container::set_focus(bool focus)
{
	has_focus_ = focus;
	if (focused_child_) {
		focused_child_->set_focus(focus);
	}
}

bool ui_container::focus_first()
{
	for (auto& child : children_) {
		if (child->focus_first()) {
			if (focused_child_ && focused_child_ != child.get()) focused_child_->set_focus(false);
			focused_child_ = child.get();
			focused_child_->set_focus(true);
			return true;
		}
		if (child->name() != "text" && child->name() != "opt_group" && child->name() != "dir_group" && child->name() != "scope_group" && child->name() != "orig_group") { // hacky way to check focusable
			if (focused_child_ && focused_child_ != child.get()) focused_child_->set_focus(false);
			focused_child_ = child.get();
			focused_child_->set_focus(true);
			return true;
		}
	}
	return false;
}

bool ui_container::focus_last()
{
	for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
		auto& child = *it;
		if (child->focus_last()) {
			if (focused_child_ && focused_child_ != child.get()) focused_child_->set_focus(false);
			focused_child_ = child.get();
			focused_child_->set_focus(true);
			return true;
		}
		if (child->name() != "text" && child->name() != "opt_group" && child->name() != "dir_group" && child->name() != "scope_group" && child->name() != "orig_group") {
			if (focused_child_ && focused_child_ != child.get()) focused_child_->set_focus(false);
			focused_child_ = child.get();
			focused_child_->set_focus(true);
			return true;
		}
	}
	return false;
}

bool ui_container::focus_next()
{
	if (children_.empty()) return false;
	
	if (focused_child_) {
		if (focused_child_->focus_next()) return true;

		auto it = std::find_if(children_.begin(), children_.end(), 
			[this](const std::unique_ptr<ui_element>& p) { return p.get() == focused_child_; });
		
		auto next_it = it;
		if (next_it != children_.end()) ++next_it;
		
		while (next_it != children_.end()) {
			if ((*next_it)->focus_first()) {
				focused_child_->set_focus(false);
				focused_child_ = next_it->get();
				focused_child_->set_focus(true);
				return true;
			}
			if ((*next_it)->name() != "text" && (*next_it)->name() != "opt_group" && (*next_it)->name() != "dir_group" && (*next_it)->name() != "scope_group" && (*next_it)->name() != "orig_group") {
				focused_child_->set_focus(false);
				focused_child_ = next_it->get();
				focused_child_->set_focus(true);
				return true;
			}
			++next_it;
		}
		
		// If we are the root dialog, wrap around
		if (name_ == "dialog" || name_ == "Force Quit" || name_ == "Question" || name_ == "Unsaved Changes" || name_ == "Find" || name_ == "Replace") {
			focused_child_->set_focus(false);
			focus_first();
			return true;
		}
		
		return false;
	} else {
		return focus_first();
	}
}

bool ui_container::focus_previous()
{
	if (children_.empty()) return false;
	
	if (focused_child_) {
		if (focused_child_->focus_previous()) return true;

		auto it = std::find_if(children_.begin(), children_.end(), 
			[this](const std::unique_ptr<ui_element>& p) { return p.get() == focused_child_; });
		
		if (it != children_.begin()) {
			auto prev_it = std::prev(it);
			while (true) {
				if ((*prev_it)->focus_last()) {
					focused_child_->set_focus(false);
					focused_child_ = prev_it->get();
					focused_child_->set_focus(true);
					return true;
				}
				if ((*prev_it)->name() != "text" && (*prev_it)->name() != "opt_group" && (*prev_it)->name() != "dir_group" && (*prev_it)->name() != "scope_group" && (*prev_it)->name() != "orig_group") {
					focused_child_->set_focus(false);
					focused_child_ = prev_it->get();
					focused_child_->set_focus(true);
					return true;
				}
				if (prev_it == children_.begin()) break;
				--prev_it;
			}
		}
		
		if (name_ == "dialog" || name_ == "Force Quit" || name_ == "Question" || name_ == "Unsaved Changes" || name_ == "Find" || name_ == "Replace") {
			focused_child_->set_focus(false);
			focus_last();
			return true;
		}
		
		return false;
	} else {
		return focus_last();
	}
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
	attrset(COLOR_PAIR(5));
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
	
	std::string suggestion;
	if (has_focus_ && autocomplete_provider_ && !buffer_.empty()) {
	    suggestion = autocomplete_provider_(buffer_);
	}

	std::string full_display = display_text;
	if (!suggestion.empty() && suggestion.length() > display_text.length()) {
	    full_display = suggestion; // If suggestion is longer, we might draw it
	}

	if (static_cast<int>(full_display.length()) > width_) {
		full_display = full_display.substr(0, width_);
	}

	mvaddstr(abs_y, abs_x, display_text.c_str());
	if (!suggestion.empty() && suggestion.length() > buffer_.length()) {
	    attrset(COLOR_PAIR(4)); // Fallback color for autocomplete
	    int sug_x = abs_x + buffer_.length() - display_offset;
	    if (sug_x < abs_x + width_) {
	        std::string sug_draw = suggestion.substr(buffer_.length());
	        if (sug_x + static_cast<int>(sug_draw.length()) > abs_x + width_) {
	            sug_draw = sug_draw.substr(0, abs_x + width_ - sug_x);
	        }
	        mvaddstr(abs_y, sug_x, sug_draw.c_str());
	    }
	    attrset(COLOR_PAIR(5));
	}

	if (has_focus_) {
		int cursor_x = abs_x + cursor_pos_ - display_offset;
		if (cursor_x >= abs_x && cursor_x < abs_x + width_) {
			attron(COLOR_PAIR(14)); // Black text on green bg for cursor
			char c = ' ';
			if (cursor_pos_ < static_cast<int>(buffer_.length())) {
				c = buffer_[cursor_pos_];
			} else if (cursor_pos_ == static_cast<int>(buffer_.length()) && !suggestion.empty() && suggestion.length() > buffer_.length()) {
			    c = suggestion[buffer_.length()];
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
				if (autocomplete_provider_ && !buffer_.empty()) {
				    std::string sug = autocomplete_provider_(buffer_);
				    if (!sug.empty() && sug.length() > buffer_.length() && cursor_pos_ == static_cast<int>(buffer_.length())) {
				        set_buffer(sug);
				        return true;
				    }
				}
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
				ui_element* p = parent_;
				while (p) { if (p->focus_next()) break; p = p->parent(); }
				return true;
			}
			if (ev.key_code == KEY_UP || ev.key_code == KEY_BTAB) {
				ui_element* p = parent_;
				while (p) { if (p->focus_previous()) break; p = p->parent(); }
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
		
		if (hotkey_ != '\0' && (ev.key_code == -hotkey_ || ev.key_code == -tolower(hotkey_) || ev.key_code == -toupper(hotkey_) || (has_focus_ && (ev.key_code == hotkey_ || ev.key_code == tolower(hotkey_) || ev.key_code == toupper(hotkey_))))) {
			set_pressed(true);
			if (on_click_) on_click_();
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
	if (checked_) {
		addch('X');
	} else {
		addch(' ');
	}
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
	if (selected_) {
		addstr("•");
	} else {
		addch(' ');
	}
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
		
		if (hotkey_ != '\0' && (ev.key_code == -hotkey_ || ev.key_code == -tolower(hotkey_) || ev.key_code == -toupper(hotkey_) || (has_focus_ && (ev.key_code == hotkey_ || ev.key_code == tolower(hotkey_) || ev.key_code == toupper(hotkey_))))) {
			selected_ = true;
			if (parent_) {
				parent_->set_focus_by_name(name_);
				parent_->child_got_selected(this);
			}
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
	// Also allow children to be queried by their direct name
	for (const auto& child : children_) {
		auto val = child->get_value(target_name);
		if (val) return val;
	}
	return std::nullopt;
}

// --- ui_group_box ---

ui_group_box::ui_group_box(std::string name, int x, int y, int width, int height, const std::string &title)
    : ui_container(std::move(name), x, y, width, height), title_(title)
{
}

void ui_group_box::draw(int abs_x, int abs_y) const
{
	attrset(COLOR_PAIR(17));
	for (int i = 1; i < height_; ++i) {
		move(abs_y + i, abs_x);
		for (int j = 0; j < width_; ++j)
			addch(' ');
	}

	if (!title_.empty()) {
		attrset(COLOR_PAIR(1));
		mvaddstr(abs_y, abs_x, title_.c_str());
	}
	attrset(0);
	
	// Draw children
	ui_container::draw(abs_x, abs_y);
}

#include "fs_utils.h"
#include <sys/stat.h>
#include <algorithm>

// --- ui_fileselector ---

ui_fileselector::ui_fileselector(std::string name, int x, int y, int width, int height, 
					const std::string& initial_path,
					std::function<void(const std::string&)> on_selection_changed,
					std::function<void(const std::string&)> on_submit)
	: ui_element(std::move(name), x, y, width, height), on_selection_changed_(std::move(on_selection_changed)), on_submit_(std::move(on_submit))
{
	try {
		if (!initial_path.empty() && fs::is_directory(initial_path)) {
			current_path_ = fs_utils::safe_absolute(initial_path);
		} else if (!initial_path.empty()) {
			current_path_ = fs_utils::safe_absolute(fs::path(initial_path).parent_path());
			if (current_path_.empty()) current_path_ = fs::current_path();
		} else {
			current_path_ = fs::current_path();
		}
		populate_files();
	} catch (...) {
		current_path_ = fs::current_path();
		populate_files();
	}
}

void ui_fileselector::populate_files()
{
	files_.clear();
	selected_index_ = 0;
	scroll_top_ = 0;

	try {
		if (fs::exists(current_path_) && fs::is_directory(current_path_)) {
			if (current_path_.has_parent_path()) {
				file_entry p;
				p.path = current_path_.parent_path();
				p.display_name = "../";
				p.is_dir = true;
				p.size = 0;
				p.mtime = std::chrono::system_clock::now();
				files_.push_back(p);
			}

			std::vector<file_entry> directories;
			std::vector<file_entry> files;

			for (const auto &entry : fs::directory_iterator(current_path_)) {
				if (entry.path().filename().string().starts_with("."))
					continue;

				file_entry fe;
				fe.path = entry.path();
				fe.is_dir = entry.is_directory();

				struct stat attr;
				if (stat(entry.path().string().c_str(), &attr) == 0) {
					fe.mtime = std::chrono::system_clock::from_time_t(attr.st_mtime);
					fe.size = fe.is_dir ? 0 : attr.st_size;
				} else {
					fe.mtime = std::chrono::system_clock::now();
					fe.size = 0;
				}

				if (fe.is_dir) {
					fe.display_name = entry.path().filename().string() + "/";
					directories.push_back(fe);
				} else {
					fe.display_name = entry.path().filename().string();
					files.push_back(fe);
				}
			}

			std::sort(directories.begin(), directories.end(), [](const auto &a, const auto &b) { return a.display_name < b.display_name; });
			std::sort(files.begin(), files.end(), [](const auto &a, const auto &b) { return a.display_name < b.display_name; });

			files_.insert(files_.end(), directories.begin(), directories.end());
			files_.insert(files_.end(), files.begin(), files.end());
		}
	} catch (...) {
	}
	
	if (!files_.empty() && on_selection_changed_) {
		on_selection_changed_(files_[selected_index_].display_name);
	}
}

void ui_fileselector::set_current_path(const fs::path& path)
{
	current_path_ = path;
	populate_files();
}

std::optional<file_entry> ui_fileselector::get_selected_entry() const
{
	if (selected_index_ >= 0 && selected_index_ < static_cast<int>(files_.size())) {
		return files_[selected_index_];
	}
	return std::nullopt;
}

std::string ui_fileselector::get_autocomplete_suggestion(const std::string& buffer) const
{
	if (buffer.empty()) return "";

	for (const auto &fe : files_) {
		if (!fe.is_dir && fe.display_name == buffer) return "";
		if (fe.is_dir && fe.display_name == buffer + "/") return "";
	}

	std::string suggestion = "";
	std::chrono::system_clock::time_point newest_mtime = std::chrono::system_clock::time_point::min();
	for (const auto &fe : files_) {
		if (!fe.is_dir) {
			if (fe.display_name.starts_with(buffer)) {
				if (fe.mtime >= newest_mtime) {
					newest_mtime = fe.mtime;
					suggestion = fe.display_name;
				}
			}
		}
	}
	return suggestion;
}

void ui_fileselector::draw(int abs_x, int abs_y) const
{
	int list_box_height = height_ - 1; // 7 rows for files, 1 for scrollbar
	int col_width = (width_ - 2) / 2; // e.g. (46 - 2)/2 = 22

	for (int i = 0; i < list_box_height; ++i) {
		move(abs_y + i, abs_x);
		attrset(COLOR_PAIR(17)); // Black on Cyan
		for (int j = 0; j < width_; ++j) addch(' ');
		mvaddstr(abs_y + i, abs_x + col_width, "│");
	}

	for (int i = 0; i < list_box_height; ++i) {
		for (int col = 0; col < 2; ++col) {
			int file_idx = scroll_top_ + i + (col * list_box_height);
			if (file_idx < static_cast<int>(files_.size())) {
				bool is_sel = (file_idx == selected_index_);

				if (is_sel && has_focus_) attrset(COLOR_PAIR(18)); // Bright Yellow on Cyan
				else attrset(COLOR_PAIR(17));

				int draw_col_width = (col == 0) ? col_width : (width_ - col_width - 1);
				std::string name = files_[file_idx].display_name;
				if (name.length() > static_cast<size_t>(draw_col_width)) {
					name = name.substr(0, draw_col_width);
				}
				int draw_x = abs_x + (col * (col_width + 1));
				mvaddstr(abs_y + i, draw_x, name.c_str());
			}
		}
	}

	// Scrollbar
	attrset(COLOR_PAIR(17));
	move(abs_y + list_box_height, abs_x);
	addstr("◄");
	for (int j = 1; j < width_ - 1; ++j) addstr("░");
	addstr("►");
	if (!files_.empty()) {
		int max_scroll = std::max(1, static_cast<int>(files_.size()) - list_box_height * 2);
		int thumb_pos = 1;
		if (max_scroll > 0) {
			thumb_pos = 1 + (scroll_top_ * (width_ - 3)) / max_scroll;
		}
		if (thumb_pos >= width_ - 1) thumb_pos = width_ - 2;
		mvaddstr(abs_y + list_box_height, abs_x + thumb_pos, "■");
	}
	attrset(0);
}

bool ui_fileselector::handle_event(const editor_event &ev, int abs_x, int abs_y)
{
	if (ev.type == event_type::key_press && has_focus_) {
		int files_height = height_ - 1;
		switch (ev.key_code) {
			case KEY_UP:
				if (selected_index_ > 0) selected_index_--;
				break;
			case KEY_DOWN:
				if (selected_index_ < static_cast<int>(files_.size()) - 1) selected_index_++;
				break;
			case KEY_LEFT:
				if (selected_index_ >= files_height) selected_index_ -= files_height;
				break;
			case KEY_RIGHT:
				if (selected_index_ + files_height < static_cast<int>(files_.size()))
					selected_index_ += files_height;
				else
					selected_index_ = static_cast<int>(files_.size()) - 1;
				break;
			case KEY_ENTER:
			case 10:
			case 13:
				if (selected_index_ >= 0 && selected_index_ < static_cast<int>(files_.size())) {
					const auto &entry = files_[selected_index_];
					if (entry.is_dir) {
						current_path_ = fs::canonical(entry.path);
						populate_files();
					} else {
						if (on_submit_) on_submit_(entry.display_name);
					}
					return true;
				}
				break;
			case '\t':
				if (parent_) {
					ui_element* p = parent_;
					while (p) { if (p->focus_next()) break; p = p->parent(); }
				}
				return true;
			case KEY_BTAB:
				if (parent_) {
					ui_element* p = parent_;
					while (p) { if (p->focus_previous()) break; p = p->parent(); }
				}
				return true;
			default:
				return false;
		}

		while (selected_index_ < scroll_top_) scroll_top_ -= files_height;
		while (selected_index_ >= scroll_top_ + files_height * 2) scroll_top_ += files_height;

		if (selected_index_ >= 0 && selected_index_ < static_cast<int>(files_.size())) {
			if (on_selection_changed_) on_selection_changed_(files_[selected_index_].display_name);
		}
		return true;
	}
	
	if (ev.type == event_type::mouse_click && contains_coordinate(ev.mouse_x, ev.mouse_y, abs_x, abs_y)) {
		if (parent_) parent_->set_focus_by_name(name_);
		int files_height = height_ - 1;
		int rel_y = ev.mouse_y - abs_y;
		int rel_x = ev.mouse_x - abs_x;
		int col_width = (width_ - 2) / 2;
		
		if (rel_y < files_height) {
			int col = (rel_x > col_width) ? 1 : 0;
			int clicked_idx = scroll_top_ + rel_y + (col * files_height);
			if (clicked_idx < static_cast<int>(files_.size())) {
				// If clicking already selected item, act like enter
				if (clicked_idx == selected_index_) {
					const auto &entry = files_[selected_index_];
					if (entry.is_dir) {
						current_path_ = fs::canonical(entry.path);
						populate_files();
					} else {
						if (on_submit_) on_submit_(entry.display_name);
					}
				} else {
					selected_index_ = clicked_idx;
					if (on_selection_changed_) on_selection_changed_(files_[selected_index_].display_name);
				}
				return true;
			}
		}
		return true; // handled click inside us
	}

	return false;
}

// --- ui_file_info_panel ---

ui_file_info_panel::ui_file_info_panel(int x, int y, int width, ui_fileselector* fs_view)
    : ui_element("file_info_panel", x, y, width, 2), fs_view_(fs_view)
{
}

void ui_file_info_panel::draw(int abs_x, int abs_y) const
{
	if (!fs_view_) return;

	attrset(COLOR_PAIR(5));
	for (int i = 0; i < 2; ++i) {
		move(abs_y + i, abs_x);
		for (int j = 0; j < width_; ++j) addch(' ');
	}

	std::string path_str = fs_view_->get_current_path().string();
	if (path_str.length() > static_cast<size_t>(width_ - 2)) {
		path_str = path_str.substr(path_str.length() - (width_ - 2));
	}
	mvaddstr(abs_y, abs_x + 1, path_str.c_str());

	auto sel_entry = fs_view_->get_selected_entry();
	if (sel_entry) {
		std::string info_str = sel_entry->display_name;
		info_str += "  " + std::to_string(sel_entry->size);

		std::time_t t = std::chrono::system_clock::to_time_t(sel_entry->mtime);
		std::tm *tm = std::localtime(&t);
		char time_buf[64];
		std::strftime(time_buf, sizeof(time_buf), "%b %e, %Y %I:%M%p", tm);
		info_str += "  ";
		info_str += time_buf;
		mvaddstr(abs_y + 1, abs_x + 1, info_str.c_str());
	}
	attrset(0);
}
