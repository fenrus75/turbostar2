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

        // Verify ESC]0;Title\x07 and ESC]11;?\x1b\\ don't print anything
        term.write("\x1b]0;Title\x07");
        term.write("\x1b]11;?\x1b\\");
        for (int x = 0; x < 10; ++x) {
            assert_equal(term.get_grid()[2][x].glyph, ' ', "OSC character not printed");
        }
    }

    // 6. BackColorErase (BCE) during erase and scroll operations
    {
        ui::ansi_terminal_emulator term(5, 3);
        // Set background color to Blue (4) and write some text
        term.write("\x1b[44mABC");
        // Grid row 0 has glyphs 'A', 'B', 'C' with bg = 4
        assert_equal(term.get_grid()[0][0].bg, 4, "Initial cell bg is blue");

        // Clear to end of line (CSI K) at cursor position (0, 3)
        term.write("\x1b[K");
        // Remaining cells in row 0 (index 3 and 4) should be cleared with blue background
        assert_equal(term.get_grid()[0][3].bg, 4, "Cleared part of line bg is blue");
        assert_equal(term.get_grid()[0][3].glyph, ' ', "Cleared cell glyph is space");

        // Clear entire display (CSI 2 J) - entire grid should become blue background
        term.write("\x1b[2J");
        for (int y = 0; y < 3; ++y) {
            for (int x = 0; x < 5; ++x) {
                assert_equal(term.get_grid()[y][x].bg, 4, "Cleared display cell bg is blue");
            }
        }

        // Test scrolling with active background color
        // Write to fill the screen and trigger scroll
        term.write("12345\n67890\nabcde\nf"); // Scroll up happens
        // The newly scrolled-in line at index 2 should have blue background
        assert_equal(term.get_grid()[2][4].bg, 4, "Scrolled-in cell bg is blue");
    }

    // 7. CNL (Cursor Next Line) and CPL (Cursor Previous Line)
    {
        ui::ansi_terminal_emulator term(10, 5);
        // Position cursor at (2, 2)
        term.write("\x1b[3;3H");
        assert_equal(term.get_cursor_x(), 2, "Start cursor X");
        assert_equal(term.get_cursor_y(), 2, "Start cursor Y");

        // Cursor Next Line (CNL) with 0 param (should move to start of next row: (0, 3))
        term.write("\x1b[0E");
        assert_equal(term.get_cursor_x(), 0, "CNL X");
        assert_equal(term.get_cursor_y(), 3, "CNL Y");

        // Cursor Previous Line (CPL) with omitted param (should move to start of previous row: (0, 2))
        term.write("\x1b[F");
        assert_equal(term.get_cursor_x(), 0, "CPL X");
        assert_equal(term.get_cursor_y(), 2, "CPL Y");
    }

    // 8. CHA (Cursor Horizontal Absolute) and VPA (Vertical Line Position Absolute)
    {
        ui::ansi_terminal_emulator term(10, 5);
        // Position cursor at (2, 2)
        term.write("\x1b[3;3H");

        // Cursor Horizontal Absolute (CHA) with 0 param (defaults to col 1 -> index 0)
        term.write("\x1b[0G");
        assert_equal(term.get_cursor_x(), 0, "CHA with 0 X");
        assert_equal(term.get_cursor_y(), 2, "CHA with 0 Y");

        // Cursor Horizontal Absolute (CHA) with parameter 5 (col 5 -> index 4)
        term.write("\x1b[5G");
        assert_equal(term.get_cursor_x(), 4, "CHA with 5 X");

        // Vertical Line Position Absolute (VPA) with omitted param (defaults to row 1 -> index 0)
        term.write("\x1b[d");
        assert_equal(term.get_cursor_y(), 0, "VPA default Y");

        // Vertical Line Position Absolute (VPA) with parameter 4 (row 4 -> index 3)
        term.write("\x1b[4d");
        assert_equal(term.get_cursor_y(), 3, "VPA with 4 Y");
    }

    // 9. ECH (Erase Character)
    {
        ui::ansi_terminal_emulator term(10, 5);
        // Write some text
        term.write("HelloWorld");
        assert_equal(term.get_grid()[0][4].glyph, 'o', "Before ECH");

        // Reposition to (0, 3) (letter 'l' in Hello)
        term.write("\x1b[1;4H");

        // Erase 3 characters (indexes 3, 4, 5: 'l', 'o', 'W')
        term.write("\x1b[3X");
        // Verify characters erased to spaces
        assert_equal(term.get_grid()[0][2].glyph, 'l', "Index 2 unchanged");
        assert_equal(term.get_grid()[0][3].glyph, ' ', "Index 3 erased");
        assert_equal(term.get_grid()[0][4].glyph, ' ', "Index 4 erased");
        assert_equal(term.get_grid()[0][5].glyph, ' ', "Index 5 erased");
        assert_equal(term.get_grid()[0][6].glyph, 'o', "Index 6 unchanged");

        // Verify cursor did not move (should still be at (0, 3))
        assert_equal(term.get_cursor_x(), 3, "Cursor X after ECH");
        assert_equal(term.get_cursor_y(), 0, "Cursor Y after ECH");
    }

    std::cout << "test_ansi_terminal_emulator passed!\n";
    return 0;
}

