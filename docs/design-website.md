# Turbostar Website Design & Update Guidelines

This document outlines the architecture, styling conventions, layout patterns, and update rules for the Turbostar project website located in the `docs/` directory.

---

## 1. Directory Structure

The website is implemented as a static site hosted from the `docs/` directory:
- **Pages**:
  - [index.html](file:///home/arjan/git/turbostar2/docs/index.html): Website homepage, highlighting core capabilities (TUI, LSP/Git, Agent, Command Center) and installation quickstart.
  - [editor.html](file:///home/arjan/git/turbostar2/docs/editor.html): Deep dive into developer-native features (Multi-Window layout, GDB Debugging, Crash Catcher, and Integrated Hex Editor), plus keyboard shortcut reference.
  - [ai.html](file:///home/arjan/git/turbostar2/docs/ai.html): Details regarding agentic workspace integrations, virtual context paging, undo history, and subagent controls.
- **Assets & Styling**:
  - [style.css](file:///home/arjan/git/turbostar2/docs/style.css): Main stylesheet containing the responsive Borland-inspired design system.
  - [overlay.js](file:///home/arjan/git/turbostar2/docs/overlay.js): Interceptor script for modal lightbox image preview.
  - **Screenshots & Logos**: Image assets (e.g., `hexeditor.png`, `screenshot-debugger.png`) showing real TUI layouts.

---

## 2. Design System & Aesthetics

### Color Palette (HSL)
The styling in `style.css` uses HSL tokens to maintain a vibrant, retro-modern aesthetic based on the Turbo Pascal 7 color palette:
- `--bg-primary`: Deep space blue background (`hsl(220, 50%, 6%)`).
- `--bg-secondary`: Container/Card background (`hsl(218, 45%, 12%)`).
- `--accent-blue`: Vibrant classic blue (`hsl(215, 95%, 45%)`).
- `--accent-cyan`: Electric cyan highlight (`hsl(185, 100%, 50%)`).
- `--accent-yellow`: Retro yellow (`hsl(45, 100%, 65%)`).
- `--accent-green`: Borland button green (`hsl(140, 70%, 45%)`).
- `--accent-red`: Delete / warning red (`hsl(0, 85%, 55%)`).

---

## 3. Key Layout Patterns

### A. Alternating Feature Splits
Features on detail pages (`editor.html`, `ai.html`) use the `.detail-split` layout (flexbox with 50% info, 50% visual mockup). Adjacent sections should alternate layout directions to create a dynamic, professional flow:
- **Standard**: `<section class="detail-split">` (Info on left, Visual on right).
- **Reverse**: `<section class="detail-split reverse">` (Visual on left, Info on right).

### B. Terminal Mockup Wrapper
All screenshots must be wrapped in a terminal mockup to simulate an active terminal application:
```html
<div class="terminal-mockup" id="mock-<feature-id>">
  <div class="terminal-header">
    <div class="terminal-dots">
      <span class="terminal-dot close"></span>
      <span class="terminal-dot minimize"></span>
      <span class="terminal-dot maximize"></span>
    </div>
    <div class="terminal-title">turbostar --some-flag</div>
    <div></div>
  </div>
  <div class="terminal-content">
    <a href="./screenshot-name.png" target="_blank">
      <img src="./screenshot-name.png" alt="Meaningful Caption Describing the Image">
    </a>
  </div>
</div>
```

---

## 4. Overlay & Lightbox Integration

- The `overlay.js` script runs automatically on DOM load, selecting all `<a>` tags pointing to `.png`, `.jpg`, or `.jpeg` files.
- It overrides the default click behavior to display a smooth, glassmorphism modal (`#image-overlay-modal`) with a blurred backdrop.
- **Caption Generation**: The overlay extracts the `alt` attribute of the inner `<img>` tag and renders it as the modal caption text.
- **Rule**: Every screenshot link MUST contain a descriptive `alt` attribute inside the `img` tag to ensure proper captioning in the lightbox modal.

---

## 5. Maintenance & Update Rules for Agents

1. **Keep Navigation Menus Synced**: When adding pages or updating navigation links, changes must be replicated across `<header>` and `<footer>` elements in `index.html`, `editor.html`, and `ai.html`.
2. **Layout Consistency**: Always use existing CSS grid (`.grid`), card (`.card`), and split-layout (`.detail-split`) styles. Avoid inline styling or custom CSS overrides where possible.
3. **Responsive Design**: Ensure all media queries in `style.css` are respected. Validate that any new markup scales down cleanly to mobile widths (e.g., flex directions collapse to columns on screens `< 900px`).
4. **Log Updates**: Update the Done section in `docs/todo.md` under the correct completion date upon deploying website changes.
