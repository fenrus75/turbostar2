#include "ui/components/ui_tabbed_container.h"
#include <algorithm>
#include <ncurses.h>

ui_tabbed_container::ui_tabbed_container(std::string name, int x, int y, int width, int height)
    : ui_container(std::move(name), x, y, width, height)
{
	sidebar_list_ = std::make_unique<ui_listbox>("sidebar_list", 0, 0, sidebar_width_, height, nullptr, nullptr);
	sidebar_list_->set_parent(this);
}

ui_tabbed_container::ui_tabbed_container(std::string name)
    : ui_container(std::move(name), 0, 0, 0, 0)
{
	sidebar_list_ = std::make_unique<ui_listbox>("sidebar_list", 0, 0, sidebar_width_, 0, nullptr, nullptr);
	sidebar_list_->set_parent(this);
}

void ui_tabbed_container::recalculate_sidebar_width()
{
	size_t max_title_len = 10;
	for (const auto &p : pages_) {
		if (p.title.length() > max_title_len) {
			max_title_len = p.title.length();
		}
	}
	// Add padding for selection indicator "► " and margin
	int calculated = static_cast<int>(max_title_len) + 4;
	// Enforce min width of 14 and soft max guideline of 20
	sidebar_width_ = std::clamp(calculated, 14, 22);

	std::vector<std::string> titles;
	for (const auto &p : pages_) {
		titles.push_back(p.title);
	}
	sidebar_list_->set_items(titles);
	sidebar_list_->set_selected_index(static_cast<int>(active_tab_));
}

void ui_tabbed_container::add_tab_page(const std::string &id, const std::string &title, std::unique_ptr<ui_element> page)
{
	if (!page)
		return;

	page->set_parent(this);
	pages_.push_back(tab_page_item{id, title, std::move(page)});
	recalculate_sidebar_width();
	flow();
}

ui_element *ui_tabbed_container::get_tab_page(size_t index) const
{
	if (index < pages_.size()) {
		return pages_[index].content.get();
	}
	return nullptr;
}

ui_element *ui_tabbed_container::get_active_page() const
{
	return get_tab_page(active_tab_);
}

bool ui_tabbed_container::set_active_tab(size_t index)
{
	if (index >= pages_.size()) {
		return false;
	}

	size_t prev = active_tab_;
	active_tab_ = index;
	sidebar_list_->set_selected_index(static_cast<int>(index));

	if (prev != active_tab_ && tab_changed_cb_) {
		tab_changed_cb_(active_tab_, pages_[active_tab_].id);
	}

	flow();
	return true;
}

bool ui_tabbed_container::set_active_tab_by_id(const std::string &id)
{
	for (size_t i = 0; i < pages_.size(); ++i) {
		if (pages_[i].id == id) {
			return set_active_tab(i);
		}
	}
	return false;
}

bool ui_tabbed_container::flow()
{
	recalculate_sidebar_width();

	int available_h = height();
	int content_x = sidebar_width_ + 1;
	int content_w = std::max(0, width() - content_x);

	if (sidebar_list_) {
		sidebar_list_->set_bounds(0, 0, sidebar_width_, available_h);
		sidebar_list_->flow();
	}

	for (size_t i = 0; i < pages_.size(); ++i) {
		if (pages_[i].content) {
			pages_[i].content->set_bounds(content_x, 0, content_w, available_h);
			pages_[i].content->flow();
		}
	}

	return true;
}

int ui_tabbed_container::natural_width() const
{
	int max_content_w = 0;
	for (const auto &p : pages_) {
		if (p.content) {
			max_content_w = std::max(max_content_w, p.content->natural_width());
		}
	}
	return sidebar_width_ + 1 + max_content_w;
}

int ui_tabbed_container::natural_height() const
{
	int max_content_h = static_cast<int>(pages_.size());
	for (const auto &p : pages_) {
		if (p.content) {
			max_content_h = std::max(max_content_h, p.content->natural_height());
		}
	}
	return std::max(6, max_content_h);
}

void ui_tabbed_container::draw(int abs_x, int abs_y) const
{
	// 1. Draw left sidebar
	if (sidebar_list_) {
		sidebar_list_->draw(abs_x, abs_y);
	}

	// 2. Draw vertical separator line
	int divider_x = abs_x + sidebar_width_;
	for (int y_off = 0; y_off < height(); ++y_off) {
		mvaddch(abs_y + y_off, divider_x, ACS_VLINE);
	}

	// 3. Draw active page content
	if (auto active = get_active_page()) {
		active->draw(abs_x + sidebar_width_ + 1, abs_y);
	}
}

bool ui_tabbed_container::handle_event(const editor_event &ev, int abs_x, int abs_y)
{
	if (ev.type == event_type::mouse_click || ev.type == event_type::mouse_drag) {
		if (ev.mouse_x < abs_x + sidebar_width_) {
			// Clicked in sidebar area
			if (sidebar_list_ && sidebar_list_->handle_event(ev, abs_x, abs_y)) {
				int sel = sidebar_list_->get_selected_index();
				if (sel >= 0 && static_cast<size_t>(sel) < pages_.size()) {
					set_active_tab(static_cast<size_t>(sel));
				}
				set_focused_child(sidebar_list_.get());
				return true;
			}
		} else if (auto active = get_active_page()) {
			// Clicked in right page area
			if (active->handle_event(ev, abs_x + sidebar_width_ + 1, abs_y)) {
				return true;
			}
		}
	}

	if (ev.type == event_type::key_press) {
		if (ev.key_code < 0) {
			// Forward Alt+Hotkey shortcuts to active page content first
			if (auto active = get_active_page()) {
				if (active->handle_event(ev, abs_x + sidebar_width_ + 1, abs_y)) {
					return true;
				}
			}
		}

		if (sidebar_list_ && sidebar_list_->has_focus()) {
			if (ev.key_code == KEY_RIGHT || ev.key_code == '\t' || ev.key_code == 10 || ev.key_code == 13) {
				// Move focus from sidebar to right page
				if (auto active = get_active_page()) {
					if (active->focus_first()) {
						sidebar_list_->set_focus(false);
						set_focused_child(active);
						return true;
					}
				}
			}

			int old_sel = sidebar_list_->get_selected_index();
			if (sidebar_list_->handle_event(ev, abs_x, abs_y)) {
				int new_sel = sidebar_list_->get_selected_index();
				if (new_sel >= 0 && new_sel != old_sel && static_cast<size_t>(new_sel) < pages_.size()) {
					set_active_tab(static_cast<size_t>(new_sel));
				}
				return true;
			}
		} else if (auto active = get_active_page()) {
			if (active->has_focus()) {
				if (ev.key_code == KEY_LEFT) {
					// Return focus to left sidebar
					active->set_focus(false);
					set_focused_child(sidebar_list_.get());
					sidebar_list_->focus_first();
					return true;
				}
				if (active->handle_event(ev, abs_x + sidebar_width_ + 1, abs_y)) {
					return true;
				}
			}
		}
	}

	return ui_container::handle_event(ev, abs_x, abs_y);
}

bool ui_tabbed_container::focus_first()
{
	if (sidebar_list_ && sidebar_list_->focus_first()) {
		set_focused_child(sidebar_list_.get());
		return true;
	}
	return false;
}

bool ui_tabbed_container::focus_last()
{
	if (auto active = get_active_page()) {
		if (active->focus_last()) {
			if (sidebar_list_) {
				sidebar_list_->set_focus(false);
			}
			set_focused_child(active);
			return true;
		}
	}
	return focus_first();
}

bool ui_tabbed_container::focus_next()
{
	if (sidebar_list_ && sidebar_list_->has_focus()) {
		if (auto active = get_active_page()) {
			if (active->focus_first()) {
				sidebar_list_->set_focus(false);
				set_focused_child(active);
				return true;
			}
		}
	} else if (auto active = get_active_page()) {
		if (active->focus_next()) {
			return true;
		}
	}
	return false;
}

bool ui_tabbed_container::focus_previous()
{
	if (auto active = get_active_page()) {
		if (active->has_focus()) {
			if (active->focus_previous()) {
				return true;
			}
			active->set_focus(false);
			if (sidebar_list_ && sidebar_list_->focus_first()) {
				set_focused_child(sidebar_list_.get());
				return true;
			}
		}
	}
	return false;
}

std::vector<ui_element *> ui_tabbed_container::get_focusable_elements()
{
	std::vector<ui_element *> elements;
	if (sidebar_list_) {
		elements.push_back(sidebar_list_.get());
	}
	if (auto active = get_active_page()) {
		auto page_elems = active->get_focusable_elements();
		elements.insert(elements.end(), page_elems.begin(), page_elems.end());
	}
	return elements;
}

std::optional<std::string> ui_tabbed_container::get_value(const std::string &target_name) const
{
	// First query active page
	if (auto active = get_active_page()) {
		auto val = active->get_value(target_name);
		if (val.has_value()) {
			return val;
		}
	}
	// Query all tab pages so form values from any tab page can be retrieved
	for (const auto &p : pages_) {
		if (p.content) {
			auto val = p.content->get_value(target_name);
			if (val.has_value()) {
				return val;
			}
		}
	}
	return std::nullopt;
}
