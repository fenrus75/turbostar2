#pragma once
#include "ui/ui_element.h"
#include <string>
#include <functional>

class ui_multiline_edit : public ui_element
{
public:
    ui_multiline_edit(std::string name, int x, int y, int width, int height,
                      std::function<void(const std::string&)> on_submit);

    void draw(int abs_x, int abs_y) const override;
    bool handle_event(const editor_event &ev, int abs_x, int abs_y) override;

    void set_buffer(const std::string& text);
    std::string get_buffer() const { return buffer_; }
    void set_on_change(std::function<void(const std::string&)> cb) { on_change_ = std::move(cb); }

    void set_focus(bool focus) override {
        ui_element::set_focus(focus);
    }

private:
    std::string buffer_;
    int cursor_pos_{0};
    int scroll_offset_{0};
    std::function<void(const std::string&)> on_submit_;
    std::function<void(const std::string&)> on_change_;
    
    // Internal helper to calculate line wrapping and cursor position
    void update_scroll();
};