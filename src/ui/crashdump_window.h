#pragma once
#include "ui/window.h"
#include "ui/components/ui_listbox.h"
#include "coredump_manager.h"
#include <memory>
#include <vector>

class coredump_window : public window {
public:
    coredump_window(int id, int x, int y, int width, int height);
    ~coredump_window() override = default;

    void draw_content() const override;
    bool process_events() override;
    void set_cursor_position() const override;

private:
    void populate_listbox();

    std::unique_ptr<ui_listbox> listbox_;
    std::vector<coredump_info> current_dumps_;
    int detail_scroll_offset_{0};
    int last_selected_index_{-1};
};