# Agent Animation Registry (`agent_animation`)

## Goals
The `agent_animation_registry` class manages ANSI / Durdraw character animations for agents and subagents in Turbostar's retro-modern Turbo Pascal TUI. It loads, stores, and serves multi-frame animated glyph models that render in the agent window status header when an agent is thinking, running tools, or idle.

## Architecture and Constraints
- **Animation Data Model**:
  - `dur_animation_data`: Contains frame rate, dimensions ($X \times Y$), and an ordered vector of `durmovie_frame` objects.
  - `durmovie_frame`: Contains frame delay in milliseconds and a 2D matrix of `durmovie_cell` structures (UTF-8 glyph string, foreground color byte, background color byte).
- **Registration Interfaces**:
  - `register_animation_json(name, json_str)`: Parses exported Durdraw JSON animation streams, validating frame dimensions and color attributes.
  - `register_animation`: Direct programmatic registration from C++ plugins.
- **Concurrency**:
  - Thread-safe registry guarded by `std::mutex mutex_`. Read operations return `std::shared_ptr<const dur_animation_data>` handles to allow lock-free rendering loops.

## Lessons Learned
- **Immutable Shared Animation Handles**: Handing out `std::shared_ptr<const dur_animation_data>` copies allows the TUI rendering loop to step through animation frames without locking the registry during frame renders.
- **Color Pair Mapping**: Translating raw 16-color ANSI codes into Turbostar's retro Turbo Pascal color palette ensures custom animations blend harmoniously with editor dialogs and syntax themes.
