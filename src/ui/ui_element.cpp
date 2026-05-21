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






