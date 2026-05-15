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

## Editor Windows

| Element | Foreground | Background | Pair | Notes |
| :--- | :--- | :--- | :--- | :--- |
| **Window Background** | Yellow/White | Dark Blue | 3 | Main text content. |
| **Window Borders** | White | Dark Blue | 3 | Double-line box drawing characters. |
| **Window Title** | White | Dark Blue | 3 | Centered on the top border. |
| **Scrollbar Track** | Cyan | Dark Blue | 4 | Dithered/shaded block characters. |
| **Scrollbar Arrows** | Cyan | Dark Blue | 4 | Scroll indicators. |
| **Window Widgets** | Green | Dark Blue | 5 | e.g., the `[■]` close button inner square. |

## Dialog Boxes

| Element | Foreground | Background | Pair | Notes |
| :--- | :--- | :--- | :--- | :--- |
| **Dialog Background** | Black | Light Gray | 1 | Main dialog content area. |
| **Dialog Borders** | White | Light Gray | (TBD) | Double-line box drawing characters. |
| **Dialog Title** | White | Light Gray | (TBD) | Centered on the top border. |
| **Dialog Shadow** | Black | Black | 6 | Offset right and down for depth. |
| **Primary Button** | White/Yellow | Green | (TBD) | e.g., the `[ OK ]` button. |
| **Button Shadow** | Black | Black | 6 | Small shadow under buttons. |
