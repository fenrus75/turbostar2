#include "ui/ui_element.h"
#include <ncurses.h>
#include <ctype.h>

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

// --- ui_button ---

ui_button::ui_button(std::string name, int x, int y, const std::string &text, char hotkey, std::function<void()> on_click)
    : ui_element(std::move(name), x, y, text.length() + 4, 1), text_(text), hotkey_(hotkey), on_click_(std::move(on_click))
{
	// width is text length + 4 for the "[ " and " ]" padding
}

void ui_button::draw(int abs_x, int abs_y) const
{
	if (has_focus_) {
		attron(COLOR_PAIR(3)); // focused green
	} else {
		attron(COLOR_PAIR(2)); // normal green
	}
	
	mvprintw(abs_y, abs_x, "[ %s ]", text_.c_str());
	
	if (hotkey_ != '\0') {
		// Try to highlight the hotkey if it exists in the text
		size_t pos = text_.find(hotkey_);
		if (pos == std::string::npos) {
			pos = text_.find(toupper(hotkey_));
		}
		if (pos == std::string::npos) {
			pos = text_.find(tolower(hotkey_));
		}
		
		if (pos != std::string::npos) {
			attron(COLOR_PAIR(4)); // hotkey color (e.g. yellow)
			mvaddch(abs_y, abs_x + 2 + pos, text_[pos]);
		}
	}
	
	attroff(COLOR_PAIR(2));
	attroff(COLOR_PAIR(3));
	attroff(COLOR_PAIR(4));
}

bool ui_button::handle_event(const editor_event &ev, int abs_x, int abs_y)
{
	if (ev.type == event_type::key_press) {
		if ((ev.key_code == '\n' || ev.key_code == '\r') && has_focus_) {
			if (on_click_) on_click_();
			return true;
		}
		
		if (hotkey_ != '\0' && (ev.key_code == hotkey_ || ev.key_code == tolower(hotkey_) || ev.key_code == toupper(hotkey_))) {
			if (on_click_) on_click_();
			return true;
		}
	}
	
	if (ev.type == event_type::mouse_click) {
		if (contains_coordinate(ev.mouse_x, ev.mouse_y, abs_x, abs_y)) {
			// Actually we might want to trigger on mouse up, but down is fine for now
			if (on_click_) on_click_();
			return true;
		}
	}
	
	return false;
}
