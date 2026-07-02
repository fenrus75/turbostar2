#pragma once
#include "ui/ui_element.h"
#include "document.h"
#include <string>
#include <functional>
#include <memory>

class ui_multiline_edit : public ui_element, public document_listener
{
public:
    ui_multiline_edit(std::string name, int x, int y, int width, int height,
                      std::function<void(const std::string&)> on_submit);
    ui_multiline_edit(std::string name, int width, int height,
                      std::function<void(const std::string&)> on_submit);
    ~ui_multiline_edit() override;

    void draw(int abs_x, int abs_y) const override;
    bool handle_event(const editor_event &ev, int abs_x, int abs_y) override;
    std::optional<std::string> get_value(const std::string &target_name) const override;
    bool is_focusable() const override { return true; }

    void set_buffer(const std::string& text);
    std::string get_buffer() const { return buffer_; }
    void set_on_change(std::function<void(const std::string&)> cb) { on_change_ = std::move(cb); }
    void set_on_external_edit(std::function<void()> cb) { on_external_edit_ = std::move(cb); }
    void link_document(std::shared_ptr<document> doc);
    void insert_text(const std::string &text);
    void set_history_enabled(bool enabled, const std::string &history_id)
    {
        history_enabled_ = enabled;
        history_id_ = history_id;
    }


    static void set_global_queue(event_queue *q) { global_queue_s_ = q; }

    // document_listener overrides
    void on_line_inserted(const std::string &/*filename*/, int /*y*/) override {}
    void on_line_deleted(const std::string &/*filename*/, int /*y*/) override {}
    void on_document_changed(const std::string &/*filename*/) override;

    void set_focus(bool focus) override {
        ui_element::set_focus(focus);
    }

    void pos_to_coord(size_t pos, size_t &line_idx, size_t &col) const;
    size_t coord_to_pos(size_t line_idx, size_t col) const;

private:
    struct visual_line {
        size_t start_idx;
        size_t length;
        bool is_continuation;
        bool ends_with_newline;
    };

    std::string buffer_;
    int cursor_pos_{0};
    int scroll_offset_{0};
    std::function<void(const std::string&)> on_submit_;
    std::function<void(const std::string&)> on_change_;
    std::function<void()> on_external_edit_;
    std::shared_ptr<document> linked_doc_;

    std::vector<visual_line> visual_lines_;
    int selection_start_{-1};
    int selection_end_{-1};
    bool selection_is_persistent_{false};
    bool k_block_mode_{false};

    void delete_selection();
    void copy_selection();
    void cut_selection();
    
    // Internal helper to calculate line wrapping and cursor position
    void update_scroll();

    bool history_enabled_{false};
    std::string history_id_;
    int history_index_{-1};
    std::string history_temp_entry_;

    static inline event_queue *global_queue_s_{nullptr};
};