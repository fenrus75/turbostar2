# syntax_color_manager

## Goals and Constraints
`syntax_color_manager` manages color pair allocations and user customizations for syntax highlighting attributes (`syntax_attribute` enum). It maps attributes to ncurses color pair indices and maintains their foreground and background color definitions.

### Constraints & Performance
- `get_color_pair()` is called in the inner per-character rendering loop in `window::draw_content()`.
- It must be extremely fast: direct `O(1)` array lookup indexed by `static_cast<size_t>(attr)` without heap allocations, locks, or pointer-chasing tree traversals.
- Color persistence is handled via JSON at `~/.cache/turbostar/colors.json`.

## Lessons Learned
- Storing color pairs in a `std::map<syntax_attribute, int>` caused red-black tree search overhead on every rendered character, consuming ~6.4% of total application CPU time during interactive scrolling and editing. Replacing this with a flat `std::array<int, max_attributes>` eliminated tree traversal overhead and made attribute pair lookups a single memory offset load.
