# Document Highlight Module Code Review

**File:** `src/document_highlight.cpp`

## Summary
The `document` class provides a background thread that processes "dirty" lines for syntax highlighting. The implementation is concise but has several issues concerning modern C++ best‑practice, thread‑safety, error handling, and documentation.

## Issues Identified
| # | Category | Description |
|---|----------|-------------|
| 1 | **Incorrect lock‑guard usage** | `std::lock_guard lock(dirty_mutex_);` and `std::unique_lock lock(dirty_mutex_);` rely on CTAD, which is not supported for `std::lock_guard`/`std::unique_lock`. This fails to compile. |
| 2 | **Missing `#include <thread>`** | The file spawns a thread (via `highlighter_thread_loop`) elsewhere, but the header does not include `<thread>` which may be required for `std::thread` declarations. |
| 3 | **Exception safety** | `process_line_highlight(l);` forwards to a plugin highlighter. If the plugin throws, the thread will terminate without cleaning up or notifying the UI. |
| 4 | **Parameter passing** | `mark_line_dirty(std::shared_ptr<line> l)` takes the shared pointer by value, unnecessarily incrementing the reference count. A `const std::shared_ptr<line>&` would be more efficient. |
| 5 | **Magic strings** | Log messages embed raw string literals. These should be constexpr or use a logging helper to avoid duplication. |
| 6 | **Thread‑stop race** | The `stop_thread_` flag is checked only after waking from `dirty_cv_`. If `stop_thread_` is set while the queue is empty, the thread may block indefinitely unless another `notify_one` is sent. |
| 7 | **Missing documentation** | Functions lack Doxygen comments, and the overall class contract for the highlight thread is undocumented. |
| 8 | **Potential dead‑lock** | The outer `if (dirty_lines_.empty())` check after processing a line holds the same `dirty_mutex_` that was used for the queue. If another thread pushes a new line while the lock is held, the notification will be missed, causing a delayed redraw. |
| 9 | **Header includes** | Some includes (`<cstdlib>`, `<cstring>`, `<regex>`) are unused and increase compile time. |
|10 | **Formatting** | The file does not follow the project's `.clang-format` (e.g., inconsistent indentation and missing spaces). |

## Recommendations
1. **Fix lock‑guard declarations**:
   ```cpp
   std::lock_guard<std::mutex> lock(dirty_mutex_);
   std::unique_lock<std::mutex> lock(dirty_mutex_);
   ```
2. **Add missing include** `#include <thread>` if thread types are used in this translation unit.
3. **Wrap highlighter call in a try/catch** to log errors and continue processing:
   ```cpp
   try { active_highlighter_->highlight(l); }
   catch (const std::exception &e) {
       event_logger::get_instance().log("Highlighter error: " + std::string(e.what()));
   }
   ```
4. **Pass shared pointer by const reference** in `mark_line_dirty` to avoid unnecessary ref‑count changes.
5. **Replace raw strings with `constexpr` constants** for log prefixes.
6. **Notify the condition variable on shutdown** to unblock the thread:
   ```cpp
   void document::stop_highlighter_thread() {
       stop_thread_ = true;
       dirty_cv_.notify_all();
   }
   ```
7. **Add Doxygen comments** for all public functions describing thread semantics and ownership.
8. **Reduce lock scope** when checking `dirty_lines_.empty()` after processing a line; only hold the lock for the minimal check and notification.
9. **Remove unused includes** (`<cstdlib>`, `<cstring>`, `<regex>`).
10. **Run clang‑format** after edits to conform to the project's formatting rules.

## TODO Checklist for Next Commit
- [ ] Update lock‑guard declarations.
- [ ] Add `<thread>` include.
- [ ] Implement exception handling around `active_highlighter_->highlight`.
- [ ] Change `mark_line_dirty` signature to `void mark_line_dirty(const std::shared_ptr<line>& l);`
- [ ] Define constexpr log prefix strings.
- [ ] Add `stop_highlighter_thread` method or ensure existing shutdown logic notifies `dirty_cv_`.
- [ ] Write Doxygen comments for all public members in `document` related to highlighting.
- [ ] Remove unused headers.
- [ ] Apply `.clang-format`.
- [ ] Add unit test for `mark_line_dirty` and thread stop behavior.
- [ ] Stage `review-highlight.md` and commit with message `docs: add code review for document_highlight.cpp`.

---
*This review follows the same style as `docs/review-document.md` and includes actionable items for the upcoming commit.*