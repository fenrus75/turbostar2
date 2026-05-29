#pragma once

#include <vector>
#include <string>
#include <cstdint>

namespace ui {

struct terminal_cell {
    char glyph = ' ';
    uint8_t fg = 7;      // default: white (ANSI 7)
    uint8_t bg = 0;      // default: black (ANSI 0)
    bool bold = false;
    bool reverse = false;
};

enum class parser_state {
    normal,
    escape,
    csi,
    escape_intermediate
};

class ansi_terminal_emulator {
public:
    ansi_terminal_emulator(int width, int height);
    ~ansi_terminal_emulator() = default;

    void resize(int width, int height);
    void write(const std::string& bytes);

    int get_width() const { return width_; }
    int get_height() const { return height_; }
    int get_cursor_x() const { return cursor_x_; }
    int get_cursor_y() const { return cursor_y_; }

    const std::vector<std::vector<terminal_cell>>& get_grid() const { return grid_; }

    void clear_all();

private:
    void process_char(char c);
    void handle_csi_command(char cmd);
    void handle_sgr();
    void scroll_up();
    void clear_line_part(int y, int start_x, int end_x);

    int width_;
    int height_;
    int cursor_x_{0};
    int cursor_y_{0};
    int saved_x_{0};
    int saved_y_{0};

    terminal_cell default_cell_;
    terminal_cell current_style_;

    std::vector<std::vector<terminal_cell>> grid_;

    parser_state state_{parser_state::normal};
    std::string esc_buffer_;
    std::vector<int> csi_params_;
    std::string current_param_str_;
};

} // namespace ui
