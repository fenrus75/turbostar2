#include <cassert>
#include <iostream>
#include "ui/ansi_terminal_emulator.h"

void assert_equal(int val, int expected, const std::string& msg) {
    if (val != expected) {
        std::cerr << "Assertion failed: " << msg << " (Got " << val << ", expected " << expected << ")\n";
        exit(1);
    }
}

int main() {
    std::cout << "Running test_ansi_terminal_emulator...\n";

    // 1. Basic write and coordinates
    {
        ui::ansi_terminal_emulator term(10, 5);
        term.write("Hello");
        assert_equal(term.get_cursor_x(), 5, "Cursor X after Hello");
        assert_equal(term.get_cursor_y(), 0, "Cursor Y after Hello");

        const auto& grid = term.get_grid();
        assert_equal(grid[0][0].glyph, 'H', "First character glyph");
        assert_equal(grid[0][4].glyph, 'o', "Fifth character glyph");
    }

    // 2. Wrap and Scroll
    {
        ui::ansi_terminal_emulator term(5, 3);
        term.write("12345"); // fills line 0
        assert_equal(term.get_cursor_x(), 5, "Cursor X after 5 chars");
        assert_equal(term.get_cursor_y(), 0, "Cursor Y after 5 chars");

        term.write("6"); // wraps to line 1
        assert_equal(term.get_cursor_x(), 1, "Cursor X after wrap");
        assert_equal(term.get_cursor_y(), 1, "Cursor Y after wrap");
        assert_equal(term.get_grid()[1][0].glyph, '6', "Wrapped character glyph");

        term.write("7890a"); // fills line 1 and wraps to line 2
        // grid currently:
        // 0: 12345
        // 1: 67890
        // 2: a....
        assert_equal(term.get_cursor_x(), 1, "Cursor X at line 2");
        assert_equal(term.get_cursor_y(), 2, "Cursor Y at line 2");

        term.write("bcde"); // fills line 2
        // grid currently:
        // 0: 12345
        // 1: 67890
        // 2: abcde
        assert_equal(term.get_cursor_x(), 5, "Cursor X at line 2 end");
        assert_equal(term.get_cursor_y(), 2, "Cursor Y at line 2 end");

        term.write("f"); // triggers scroll up!
        // grid should become:
        // 0: 67890
        // 1: abcde
        // 2: f....
        assert_equal(term.get_cursor_x(), 1, "Cursor X after scroll");
        assert_equal(term.get_cursor_y(), 2, "Cursor Y after scroll");
        assert_equal(term.get_grid()[0][0].glyph, '6', "Scrolled line 0 glyph");
        assert_equal(term.get_grid()[1][0].glyph, 'a', "Scrolled line 1 glyph");
        assert_equal(term.get_grid()[2][0].glyph, 'f', "New line 2 glyph");
    }

    // 3. VT100 Cursor repositioning & clears
    {
        ui::ansi_terminal_emulator term(10, 5);
        term.write("HelloWorld");
        term.write("\x1b[H"); // cursor home (row 1, col 1 -> (0,0))
        assert_equal(term.get_cursor_x(), 0, "Cursor X after home");
        assert_equal(term.get_cursor_y(), 0, "Cursor Y after home");

        term.write("\x1b[2;3H"); // cursor to row 2, col 3 -> (2,1) in 0-indexed? No, VT100 row 2 is index 1, col 3 is index 2.
        // wait, row 2 is 1, col 3 is 2.
        assert_equal(term.get_cursor_x(), 2, "Cursor X after goto");
        assert_equal(term.get_cursor_y(), 1, "Cursor Y after goto");

        term.write("A");
        assert_equal(term.get_grid()[1][2].glyph, 'A', "Char at goto position");

        term.write("\x1b[2J"); // Clear entire screen and reset cursor
        assert_equal(term.get_cursor_x(), 0, "Cursor X after clear screen");
        assert_equal(term.get_cursor_y(), 0, "Cursor Y after clear screen");
        assert_equal(term.get_grid()[1][2].glyph, ' ', "Cleared screen character");
    }

    // 4. Styles SGR SGR
    {
        ui::ansi_terminal_emulator term(10, 5);
        term.write("\x1b[1;31;44mX"); // Bold, Red FG (1), Blue BG (4)
        const auto& grid = term.get_grid();
        assert_equal(grid[0][0].glyph, 'X', "Glyph with style");
        assert_equal(grid[0][0].bold, true, "Bold set");
        assert_equal(grid[0][0].fg, 1, "Foreground color red");
        assert_equal(grid[0][0].bg, 4, "Background color blue");

        term.write("\x1b[0mY"); // Reset
        assert_equal(grid[0][1].glyph, 'Y', "Glyph with reset style");
        assert_equal(grid[0][1].bold, false, "Bold reset");
        assert_equal(grid[0][1].fg, 7, "Foreground color default");
        assert_equal(grid[0][1].bg, 0, "Background color default");
    }

    // 5. Cursor movement and ESC(B designation
    {
        ui::ansi_terminal_emulator term(10, 5);
        // Position cursor at (2, 2)
        term.write("\x1b[3;3H");
        assert_equal(term.get_cursor_x(), 2, "Start cursor X");
        assert_equal(term.get_cursor_y(), 2, "Start cursor Y");

        // Cursor Down with 0 param (should move down by 1 to row 3)
        term.write("\x1b[0B");
        assert_equal(term.get_cursor_y(), 3, "CUD with 0");

        // Cursor Down with omitted param (should move down by 1 to row 4)
        term.write("\x1b[B");
        assert_equal(term.get_cursor_y(), 4, "CUD default");

        // Cursor Up with 0 param (should move up by 1 to row 3)
        term.write("\x1b[0A");
        assert_equal(term.get_cursor_y(), 3, "CUU with 0");

        // Cursor Up with omitted param (should move up by 1 to row 2)
        term.write("\x1b[A");
        assert_equal(term.get_cursor_y(), 2, "CUU default");

        // Cursor Forward with 0 param (should move right by 1 to col 3)
        term.write("\x1b[0C");
        assert_equal(term.get_cursor_x(), 3, "CUF with 0");

        // Cursor Forward with omitted param (should move right by 1 to col 4)
        term.write("\x1b[C");
        assert_equal(term.get_cursor_x(), 4, "CUF default");

        // Cursor Backward with 0 param (should move left by 1 to col 3)
        term.write("\x1b[0D");
        assert_equal(term.get_cursor_x(), 3, "CUB with 0");

        // Cursor Backward with omitted param (should move left by 1 to col 2)
        term.write("\x1b[D");
        assert_equal(term.get_cursor_x(), 2, "CUB default");

        // Verify ESC(B doesn't print 'B'
        term.write("\x1b(B");
        assert_equal(term.get_grid()[2][2].glyph, ' ', "ESC(B character not printed");
        term.write("\x1b)0");
        assert_equal(term.get_grid()[2][2].glyph, ' ', "ESC)0 character not printed");
    }

    std::cout << "test_ansi_terminal_emulator passed!\n";
    return 0;
}

