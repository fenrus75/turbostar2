# Hex Editor Specification

This document details the architectural and UI design decisions for the `hex_editor_window` and `binary_document` classes.

## 1. Visual Layout & Rendering

The hex editor window is divided horizontally into three main sections:
- **Left Column**: File offsets rendered as fixed 8-digit hexadecimal numbers (e.g., `00000000`).
- **Middle Column**: Hex tuples representing the byte values, rendered in **uppercase** (e.g., `4A`, `0F`), separated by spaces.
- **Right Column**: Clean ASCII representation, enclosed in delimiters (e.g., `|...|`).
  - Printable characters (byte values `[32, 126]`) are displayed as-is.
  - Non-printable control characters and high-bit bytes are rendered as a dot (`.`).

### Dynamic Width Wrapping
The number of bytes displayed per line dynamically adjusts in multiples of 16 depending on the available window width:
- **16 bytes per line**: Default (minimum required width ~81 columns).
- **32 bytes per line**: Active if window width is >= 147 columns.
- **48+ bytes per line**: Scaled upwards when even wider.

### Status Bar Integration
When a hex editor window is active, the status bar displays the current cursor location in both hexadecimal and decimal formats:
- Example: `Offset: 0x000001A4 (420)`

---

## 2. Colorscheme & Visual Styling

The hex editor uses a context-aware color palette built on top of the Turbo Pascal 7 theme:
- **Offsets Column**: Bright White / Cyan (Pair 5 / Pair 4).
- **Null Bytes (`00`)**: Dim Gray (Pair 9).
- **Printable ASCII Bytes**: Standard Yellow (Pair 3).
- **Non-Printable Control Characters**: Magenta / Cyan.

---

## 3. Editing Mechanics & Focus

### Dual Column Focus
- Pressing `Tab` toggles cursor focus between the Hex tuples area and the ASCII view area.
- Cursor shape and position indicators reflect the active column.

### Typing & Input Rules
- **Hex Column**: 
  - Valid input keys are `0-9` and `A-F` (case-insensitive).
  - Editing operates at the **nibble level** (half-byte). Typing a character overwrites the high or low nibble under the cursor and advances by half a byte.
  - Arrow keys navigate by half-byte (nibble).
- **ASCII Column**:
  - Typing any printable ASCII character directly overwrites the entire byte under the cursor and advances by a full byte.
  - Arrow keys navigate by full byte.

### Append & Overwrite Strategy
- Editing is **overwrite-only** by default (inserting or deleting bytes in the middle of a file is prohibited).
- **Auto-grow at EOF**: When the cursor is positioned exactly at the end of the file (`offset == file_size`), typing a hex digit or ASCII character automatically appends a new byte, incrementing the file size by 1.

---

## 4. Document Representation & Undo History

### `binary_document` Backend
- Inherits from the existing `document` class to avoid refactoring of window classes, stubbing out/modifying text-only line APIs.
- Binary files bypass the line-based text parser and are loaded into a `std::vector<uint8_t>` buffer.
- File reading and writing are performed in binary mode (`std::ios::binary`).
- Houses its own byte-oriented undo/redo stack.

### Coalesced Undo Steps
Consecutive byte modifications are coalesced into a single undo/redo transaction to avoid bloating the history stack. The current transaction coalescing group is broken and closed when:
1. The cursor moves to a non-adjacent offset.
2. Focus toggles between the Hex and ASCII columns.
3. The document is saved to disk.

---

## 5. Integration, Auto-Detection & Limits

### Automatic Detection
- The editor automatically detects binary files during loading using `fs_utils::is_binary_file()`. If the file is binary, it is opened in Hex Editor mode using `hex_editor_window` and `binary_document`. No manual override is provided for now.

### Backup on Save
- Binary saving implements identical backup protection to text files: the original file is renamed to `filename + "~"` before the new binary data is written to disk.

### File Size Safety Limit
- A safe limit of **50MB** is enforced. Attempting to load binary files larger than 50MB will show an error dialog and abort, protecting editor performance and memory stability.

