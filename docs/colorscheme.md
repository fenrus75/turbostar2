# Turbostar Color Scheme

Based on the classic Turbo Pascal 7 interface, Turbostar uses the following primary color palettes. These are implemented using the following ncurses color pairs.

## Global Elements

| Element | Foreground | Background | Pair | Notes |
| :--- | :--- | :--- | :--- | :--- |
| **Desktop Background** | (None) | Dark Blue | 3 | The empty workspace area. |
| **Menu Bar (Top)** | Black | Light Gray | 1 | Unfocused state. |
| **Menu Hotkeys** | Red | Light Gray | 2 | The highlighted letter for quick access. |
| **Status Bar (Bottom)** | Black | Light Gray | 1 | |
| **Status Hotkeys** | Red | Light Gray | 2 | e.g., the 'F1' in 'F1 Help'. |
| **Selected Hotkey** | Red | Black | 7 | Hotkey when item is selected (reversed). |
| **Selected Menu Item** | Black | Green | 14 | Background for the active menu option. |
| **Sel. Menu Hotkey** | Red | Green | 15 | Hotkey on the active menu option. |
| **Selection Highlight** | White | Cyan | 8 | The range between Begin and End markers. |
| **Desktop Pattern** | Cyan | Dark Blue | 9 | The dithered `▒` background. |
| **Syntax: Keyword** | Bright White | Dark Blue | 12 | C++ keywords like `void`, `int`, etc. |
| **Syntax: Sel. Keyword** | Bright Yellow | Cyan | 13 | Keyword within a selection. |

## Editor Windows

| Element | Foreground | Background | Pair | Notes |
| :--- | :--- | :--- | :--- | :--- |
| **Window Background** | Yellow | Dark Blue | 3 | Main text content. |
| **Window Borders** | Bright White | Dark Blue | 5 | Focused window borders. |
| **Window Borders (Unfocused)** | White | Dark Blue | 38 | Unfocused window borders (dimmer). |
| **Window Title** | Bright White | Dark Blue | 5 | Centered on the top border. |
| **Scrollbar Track** | Cyan | Dark Blue | 4 | Dithered/shaded block characters. |
| **Scrollbar Arrows** | Cyan | Dark Blue | 4 | Scroll indicators. |
| **Window Widgets** | Bright Yellow | Dark Blue | 3 | Focused close [■] and menu [≡] icons. |
| **Window Widgets (Unfocused)** | Yellow | Dark Blue | 39 | Unfocused close [■] and menu [≡] icons. |
| **Diff: Additions** | Bright Green | Dark Blue | 30 | Lines added via fs_replace_lines. |
| **Diff: Deletions** | Bright Red | Dark Blue | 31 | Lines removed via fs_replace_lines. |
| **Diff: Header** | Bright Cyan | Dark Blue | 32 | The `@@ -X,Y +X,Y @@` context headers. |
| **Tool: Read Pending** | Bright Yellow | Cyan | 33 | Active state for `fs_read_lines`. |
| **Tool: Read Success** | Bright White | Cyan | 8 | Success state for `fs_read_lines`. |
| **Tool: Read Failure** | Bright Red | Cyan | 35 | Failure state for `fs_read_lines`. |

## Dialog Boxes

| Element | Foreground | Background | Pair | Notes |
| :--- | :--- | :--- | :--- | :--- |
| **Dialog Background** | Black | Light Gray | 1 | Main dialog content area. |
| **Dialog Borders** | Bright White | Light Gray | 11 | Double-line high-contrast borders. |
| **Dialog Title** | Black | Light Gray | 1 | Centered on the top border. |
| **Dialog Hotkeys** | Bright Yellow | Light Gray | 16 | Hotkeys for dialog elements. |
| **Dialog Group Box** | Black | Cyan | 17 | Content within a group box. |
| **Dialog Group Hotkey** | Bright Yellow | Cyan | 18 | Hotkeys within a group box. |
| **Dialog Input Field** | Bright White | Dark Blue | 5 | Text entry boxes. |
| **Focused Widget** | Black | Green | 19 | Highlight for focused radio buttons/checkboxes. |
| **Dialog Shadow** | Black | Black | 6 | Offset right and down for depth. |
| **Primary Button** | Black | Green | 10 | The signature Borland green button. |
| **Button Shadow** | Black | Black | 6 | ▀ half-block shadow for depth. |
