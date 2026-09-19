# Event Queue (`event_queue`)

## Goals
The `event_queue` class provides a synchronized, thread-safe message passing channel between asynchronous background worker threads (LSP handlers, agent tool executions, file watchers, git monitors) and Turbostar's main UI frame loop. It defines the universal `editor_event` carrier structure and `event_type` enumeration.

## Architecture and Constraints
- **Thread Safety**:
  - Pushes (`push()`) and non-blocking pops (`pop()`) are protected by a dedicated `std::mutex mutex_`.
  - Background workers can safely enqueue events from any thread without locking UI state.
- **Event Definition (`editor_event`)**:
  - Carries key codes, UTF-8 strings, mouse coordinates, priority markers, and specialized payloads (LSP diagnostic vectors, highlight text ranges, promises).
  - Integrates `std::shared_ptr<std::promise<std::string>>` for synchronous RPC-style prompts (e.g. `prompt_user`), allowing background agent threads to pause while the UI thread prompts the user and resolves the promise.
- **Central Dispatch Requirement**:
  - The central routing switch in `editor::dispatch` (`src/editor_events.cpp`) consumes all events popped from the queue.
  - **CRITICAL**: Because `editor::dispatch` terminates with a `default: break;` case, adding a new `event_type` without registering it in `editor::dispatch` causes the event to compile without warnings but be discarded silently at runtime.

## Lessons Learned
- **Non-Blocking Frame Loop**: The UI thread uses non-blocking `pop()` returning `std::optional<editor_event>`. It never waits on a condition variable inside the frame loop, preserving smooth rendering and input responsiveness.
- **Future/Promise Exception Safety**: When using promises across threads, unhandled exceptions or premature worker shutdowns must explicitly set exception states or default cancellation values on the promise to prevent calling threads from hanging indefinitely on `future.get()`.
