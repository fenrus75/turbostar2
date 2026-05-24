#pragma once
#include "ui/ui_element.h"
#include <vector>
#include <string>

namespace tools {

class ui_ask_user_group : public ui_element {
public:
    ui_ask_user_group(std::string name, int x, int y, int width, const std::vector<std::string>& options);

    void draw(int abs_x, int abs_y) const override;
    bool handle_event(const editor_event& ev, int abs_x, int abs_y) override;
    std::optional<std::string> get_value(const std::string& target_name) const override;

private:
    struct item_info {
        std::vector<std::string> lines;
        int height;
        int y_offset;
    };

    std::vector<std::string> options_;
    std::vector<item_info> items_;
    int selected_index_ = 0;
    std::string other_text_;
    int cursor_pos_ = 0;

    void layout_items();
    void wrap_text(const std::string& text, int width, std::vector<std::string>& out_lines);
};

} // namespace tools
