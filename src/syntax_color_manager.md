# syntax_color_manager

## Goals and Constraints
`syntax_color_manager` manages color pair allocations and user customizations for syntax highlighting attributes (`syntax_attribute` enum). It maps attributes to ncurses color pair indices and maintains their foreground and background color definitions.

### Constraints & Performance
- `get_color_pair()` is called in the inner per-character rendering loop in `window::draw_content()`.
- It must be extremely fast: direct `O(1)` array lookup indexed by `static_cast<size_t>(attr)` without heap allocations, locks, or pointer-chasing tree traversals.
- Color persistence is handled via JSON at `~/.cache/turbostar/colors.json`.

## Lessons Learned
- Storing color pairs in a `std::map<syntax_attribute, int>` caused red-black tree search overhead on every rendered character, consuming ~6.4% of total application CPU time during interactive scrolling and editing. Replacing this with a flat `std::array<int, max_attributes>` eliminated tree traversal overhead and made attribute pair lookups a single memory offset load.
- Comments originally defaulted to `COLOR_BLACK + 8` (dark gray) on `COLOR_BLUE` (dark blue), resulting in poor contrast (<2:1) that was nearly illegible on many terminal emulators. Changed default comment color to `COLOR_CYAN + 8` (bright cyan), providing >10:1 contrast and matching the classic Turbo Pascal / Turbo C++ IDE syntax color palette.
