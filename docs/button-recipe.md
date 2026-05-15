# Borland Style Button Implementation Guide

This guide describes how to implement buttons that match the authentic Turbo Pascal 7 / Borland C++ TUI aesthetic used in TurboStar.

## Visual Components

An authentic Borland button consists of three distinct parts:
1.  **The Button Surface**: A solid colored block (usually green).
2.  **The Side Shadow**: A full-cell black block to the right of the button.
3.  **The Bottom Shadow**: A half-cell black block flush against the bottom of the button.

## Color Pairs

Required color pairs (defined in `src/main.cpp`):
-   **PAIR(1)**: Black text on Light Gray background (Standard dialog background).
-   **PAIR(6)**: Black on Black (Full block shadow).
-   **PAIR(10)**: White text on Green background (The button itself).

## Implementation Recipe (ncurses)

### 1. Define Geometry
Calculate the center position within the parent dialog.
```cpp
std::string text = "  OK  ";
int btn_x = parent_x + (parent_width - text.length()) / 2;
int btn_y = parent_y + parent_height - 3; // Ensure 1 line gap at bottom
```

### 2. Draw Side Shadow
Use a single space character with the black-on-black pair (**Pair 6**) at the position immediately following the button text.
```cpp
attron(COLOR_PAIR(6));
mvaddstr(btn_y, btn_x + static_cast<int>(text.length()), " ");
attroff(COLOR_PAIR(6));
```

### 3. Draw Bottom Shadow (The "Half-Block")
Use the **Upper Half Block (`▀`)** character with the standard dialog background pair (**Pair 1**). 
This ensures the top half of the character cell is black (shadow) while the bottom half matches the dialog background.
The shadow should be the same length as the button text but offset by one cell to the right.
```cpp
attron(COLOR_PAIR(1));
for (int i = 0; i < static_cast<int>(text.length()); ++i) {
    mvaddstr(btn_y + 1, btn_x + 1 + i, "▀");
}
attroff(COLOR_PAIR(1));
```

### 4. Draw Button Surface
Draw the text centered on the green block.
```cpp
attron(COLOR_PAIR(10));
mvaddstr(btn_y, btn_x, text.c_str());
attroff(COLOR_PAIR(10));
```

## Behavior
Buttons should be triggered by `Enter`, `Space`, or their specific hotkey. They typically return `dialog_result::confirmed` to the parent editor.
