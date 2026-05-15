# Turbostar Color Scheme

Based on the classic Turbo Pascal 7 interface, Turbostar uses the following primary color palettes. These will be implemented using ncurses color pairs.

## Global Elements

| Element | Foreground | Background | Notes |
| :--- | :--- | :--- | :--- |
| **Desktop Background** | (None) | Dark Blue | The empty workspace area. |
| **Menu Bar (Top)** | Black | Light Gray | Unfocused state. |
| **Menu Hotkeys** | Red | Light Gray | The highlighted letter for quick access. |
| **Status Bar (Bottom)** | Black | Light Gray | |
| **Status Hotkeys** | Red | Light Gray | e.g., the 'F1' in 'F1 Help'. |

## Editor Windows

| Element | Foreground | Background | Notes |
| :--- | :--- | :--- | :--- |
| **Window Background** | Yellow/White | Dark Blue | Main text content. |
| **Window Borders** | White | Dark Blue | Double-line box drawing characters. |
| **Window Title** | White | Dark Blue | Centered on the top border. |
| **Scrollbar Track** | Cyan | Dark Blue | Dithered/shaded block characters. |
| **Scrollbar Arrows** | Cyan | Dark Blue | Scroll indicators. |
| **Window Widgets** | Green | Dark Blue | e.g., the `[■]` close button inner square. |

## Dialog Boxes

| Element | Foreground | Background | Notes |
| :--- | :--- | :--- | :--- |
| **Dialog Background** | Black | Light Gray | Main dialog content area. |
| **Dialog Borders** | White | Light Gray | Double-line box drawing characters. |
| **Dialog Title** | White | Light Gray | Centered on the top border. |
| **Dialog Shadow** | Black | Black | Offset right and down for depth. |
| **Primary Button** | White/Yellow | Green | e.g., the `[ OK ]` button. |
| **Button Shadow** | Black | Black | Small shadow under buttons. |
