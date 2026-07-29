#include "ui/components/ui_paged_container.h"
#include <algorithm>

ui_paged_container::ui_paged_container(std::string name, int x, int y, int width, int height)
    : ui_container(std::move(name), x, y, width, height)
{
	init_button_bar();
}

ui_paged_container::ui_paged_container(std::string name)
    : ui_container(std::move(name), 0, 0, 0, 0)
{
	init_button_bar();
}

void ui_paged_container::init_button_bar()
{
	button_bar_ = std::make_unique<ui_buttons_horizontal>("wizard_button_bar");
	button_bar_->set_centered(true);
	button_bar_->set_parent(this);

	update_button_states();
}

void ui_paged_container::update_button_states()
{
	if (!button_bar_)
		return;

	button_bar_->clear_children();
	back_btn_ = nullptr;
	cancel_btn_ = nullptr;
	next_btn_ = nullptr;

	if (current_page_ > 0) {
		auto back = std::make_unique<ui_button>("wizard_back", "<< ~B~ack", 'B', nullptr);
		back_btn_ = back.get();
		button_bar_->add_child(std::move(back));
	}

	auto cancel = std::make_unique<ui_button>("wizard_cancel", "~C~ancel", 'C', nullptr, true);
	cancel_btn_ = cancel.get();
	button_bar_->add_child(std::move(cancel));

	std::string next_text = "~N~ext >>";
	char next_hotkey = 'N';
	if (!pages_.empty() && current_page_ + 1 >= pages_.size()) {
		next_text = "~F~inish";
		next_hotkey = 'F';
	}
	auto next = std::make_unique<ui_button>("wizard_next", next_text, next_hotkey, nullptr);
	next_btn_ = next.get();
	button_bar_->add_child(std::move(next));

	button_bar_->flow();
}

void ui_paged_container::add_page(std::unique_ptr<ui_element> page)
{
	if (page) {
		page->set_parent(this);
		pages_.push_back(std::move(page));
		update_button_states();
	}
}

ui_element *ui_paged_container::get_page(size_t index) const
{
	if (index < pages_.size()) {
		return pages_[index].get();
	}
	return nullptr;
}

bool ui_paged_container::set_current_page(size_t page_index)
{
	if (page_index >= pages_.size()) {
		return false;
	}

	current_page_ = page_index;
	update_button_states();
	on_page_entered(current_page_);
	if (page_entered_cb_) {
		page_entered_cb_(current_page_);
	}

	auto focusables = get_focusable_elements();
	if (!focusables.empty()) {
		set_focused_child(focusables.front());
	}
	return true;
}

bool ui_paged_container::next_page()
{
	if (pages_.empty()) {
		return false;
	}

	std::string err;
	if (!on_validate_page(current_page_, err)) {
		return false;
	}
	if (validate_cb_) {
		if (!validate_cb_(current_page_, err)) {
			return false;
		}
	}

	if (current_page_ + 1 < pages_.size()) {
		return set_current_page(current_page_ + 1);
	} else if (current_page_ + 1 == pages_.size()) {
		if (next_btn_) {
			next_btn_->set_pressed(true);
		}
		return true;
	}
	return false;
}

bool ui_paged_container::previous_page()
{
	if (current_page_ > 0) {
		return set_current_page(current_page_ - 1);
	}
	return false;
}

bool ui_paged_container::on_validate_page(size_t /*page_index*/, std::string &/*out_error*/)
{
	return true;
}

void ui_paged_container::on_page_entered(size_t /*page_index*/)
{
}

void ui_paged_container::draw(int abs_x, int abs_y) const
{
	if (current_page_ < pages_.size() && pages_[current_page_]) {
		pages_[current_page_]->draw(abs_x + pages_[current_page_]->x(), abs_y + pages_[current_page_]->y());
	}
	if (button_bar_) {
		button_bar_->draw(abs_x + button_bar_->x(), abs_y + button_bar_->y());
	}
}

bool ui_paged_container::handle_event(const editor_event &ev, int abs_x, int abs_y)
{
	// 1. Process navigation button events first
	if (button_bar_) {
		bool handled = button_bar_->handle_event(ev, abs_x + button_bar_->x(), abs_y + button_bar_->y());
		if (back_btn_ && back_btn_->is_pressed()) {
			back_btn_->set_pressed(false);
			previous_page();
			return true;
		}
		if (next_btn_ && next_btn_->is_pressed()) {
			next_btn_->set_pressed(false);
			next_page();
			return true;
		}
		if (cancel_btn_ && cancel_btn_->is_pressed()) {
			return true;
		}
		if (handled) {
			return true;
		}
	}

	// 2. Process active page events
	if (current_page_ < pages_.size() && pages_[current_page_]) {
		if (pages_[current_page_]->handle_event(ev, abs_x + pages_[current_page_]->x(), abs_y + pages_[current_page_]->y())) {
			return true;
		}
	}

	return false;
}

bool ui_paged_container::flow()
{
	bool changed = false;

	int max_page_w = 0;
	int max_page_h = 0;
	for (const auto &p : pages_) {
		max_page_w = std::max(max_page_w, p->natural_width());
		max_page_h = std::max(max_page_h, p->natural_height());
	}

	int btn_w = button_bar_ ? button_bar_->natural_width() : 0;
	int req_w = std::max(max_page_w, btn_w);
	int req_h = max_page_h + 2;

	// Hybrid Grow-Only Resizing
	if (req_w > width_) {
		set_width(req_w);
		if (parent_) parent_->set_width(req_w + 4);
		changed = true;
	}
	if (req_h > height_) {
		set_height(req_h);
		if (parent_) parent_->set_height(req_h + 4);
		changed = true;
	}

	int page_area_h = std::max(0, height_ - 2);
	if (current_page_ < pages_.size() && pages_[current_page_]) {
		pages_[current_page_]->set_position(0, 0);
		if (pages_[current_page_]->width() != width_) {
			pages_[current_page_]->set_width(width_);
		}
		if (pages_[current_page_]->flow()) {
			changed = true;
		}
	}

	if (button_bar_) {
		button_bar_->set_position(0, page_area_h);
		if (button_bar_->width() != width_) {
			button_bar_->set_width(width_);
		}
		if (button_bar_->flow()) {
			changed = true;
		}
	}

	return changed;
}

int ui_paged_container::natural_width() const
{
	int max_w = 0;
	for (const auto &p : pages_) {
		max_w = std::max(max_w, p->natural_width());
	}
	int btn_w = button_bar_ ? button_bar_->natural_width() : 0;
	return std::max(max_w, btn_w);
}

int ui_paged_container::natural_height() const
{
	int max_h = 0;
	for (const auto &p : pages_) {
		max_h = std::max(max_h, p->natural_height());
	}
	return max_h + 2;
}

std::vector<ui_element *> ui_paged_container::get_focusable_elements()
{
	std::vector<ui_element *> result;
	if (current_page_ < pages_.size() && pages_[current_page_]) {
		auto page_elems = pages_[current_page_]->get_focusable_elements();
		result.insert(result.end(), page_elems.begin(), page_elems.end());
	}

	if (button_bar_) {
		auto btn_elems = button_bar_->get_focusable_elements();
		result.insert(result.end(), btn_elems.begin(), btn_elems.end());
	}
	return result;
}

std::optional<std::string> ui_paged_container::get_value(const std::string &target_name) const
{
	if (name_ == target_name) {
		return std::nullopt;
	}
	for (const auto &p : pages_) {
		if (p) {
			auto val = p->get_value(target_name);
			if (val) return val;
		}
	}
	if (button_bar_) {
		return button_bar_->get_value(target_name);
	}
	return std::nullopt;
}

std::optional<std::string> ui_paged_container::get_pressed_element_name() const
{
	if (button_bar_) {
		auto btn_pressed = button_bar_->get_pressed_element_name();
		if (btn_pressed) return btn_pressed;
	}
	if (current_page_ < pages_.size() && pages_[current_page_]) {
		return pages_[current_page_]->get_pressed_element_name();
	}
	return std::nullopt;
}
