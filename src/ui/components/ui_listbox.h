#pragma once

#include "ui/ui_element.h"
#include <string>
#include <vector>
#include <functional>

class ui_listbox : public ui_element
{
public:
    ui_listbox(std::string name, int x, int y, int width, int height,
               std::function<void(int)> on_selection_changed,
               std::function<void(int)> on_submit);

    void draw(int abs_x, int abs_y) const override;
    bool handle_event(const editor_event &ev, int abs_x, int abs_y) override;

    void set_items(const std::vector<std::string>& items);
    int get_selected_index() const { return selected_index_; }
    void set_selected_index(int index);
    void set_on_space(std::function<void(int)> on_space) { on_space_ = std::move(on_space); }
    void set_cursor_position(int abs_x, int abs_y) const;
    std::optional<std::string> get_value(const std::string &target_name) const override;

private:
    std::vector<std::string> items_;
    int selected_index_{0};
    int scroll_top_{0};

    std::function<void(int)> on_selection_changed_;
    std::function<void(int)> on_submit_;
    std::function<void(int)> on_space_;
};