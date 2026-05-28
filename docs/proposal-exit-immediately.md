# Feature Proposal: Configurable `--exit-immediately` Delay

**Date**: 2026-05-26  
**Author**: AI Assistant  
**Reference**: `docs/todo.md` line 34

---

## Current State

### Implementation

The `--exit-immediately` flag is currently implemented as a **boolean flag** in `src/main.cpp`:

```cpp
bool exit_immediately = false;
app.add_flag("--exit-immediately", exit_immediately, "Exit after 1 second");
```

The flag is passed to the `editor` constructor and stored as a member variable:

```cpp
// src/editor.h
bool exit_immediately_{false};
```

In the main editor loop (`src/editor.cpp` line 371-376), the delay is **hardcoded to 1 second**:

```cpp
if (exit_immediately_) {
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count() > 1000) {
        is_running_ = false;
    }
}
```

### Usage

```bash
# Current usage - always exits after 1 second
build/turbostar src/document.cpp --exit-immediately

# Use case: Quick startup/shutdown testing for memory leak detection
heaptrack build/turbostar src/document.cpp --exit-immediately
```

---

## Proposed Change

Make `--exit-immediately` accept an **optional numeric argument** specifying the delay in seconds:

```bash
# Exit after 1 second (default, same as current behavior)
build/turbostar src/document.cpp --exit-immediately

# Exit after 5 seconds
build/turbostar src/document.cpp --exit-immediately=5

# Exit after 30 seconds (for slow initialization scenarios)
build/turbostar src/document.cpp --exit-immediately=30

# Exit after 0.5 seconds (sub-second precision)
build/turbostar src/document.cpp --exit-immediately=0.5
```

---

## Implementation Plan

### 1. Change CLI Argument Type (src/main.cpp)

**Current**:
```cpp
bool exit_immediately = false;
app.add_flag("--exit-immediately", exit_immediately, "Exit after 1 second");
```

**Proposed**:
```cpp
std::optional<double> exit_delay;  // nullopt = flag not provided, 0.0+ = delay in seconds
app.add_option("--exit-immediately", exit_delay, "Exit after N seconds (default: 1)")->default_val(1.0)->check(CLI::Range(0.0, 3600.0));
```

**Rationale**:
- `std::optional<double>` allows distinguishing between:
  - Flag not provided (`std::nullopt`)
  - Flag provided with default value (`1.0`)
  - Flag provided with custom value (`5.0`, `0.5`, etc.)
- `CLI::Range(0.0, 3600.0)` validates input is between 0 and 3600 seconds (1 hour max)
- `default_val(1.0)` maintains backward compatibility (1 second default)

**Alternative**: If CLI11's `add_option` with `default_val` doesn't work as expected, use:
```cpp
std::optional<double> exit_delay;
app.add_option("--exit-immediately", exit_delay, "Exit after N seconds (default: 1)")->check(CLI::Range(0.0, 3600.0));
// Handle default in code:
double actual_delay = exit_delay.value_or(0.0);  // 0.0 means flag not provided
if (exit_delay.has_value()) {
    // Flag was provided, use the value (or default to 1.0 if value is 0)
    actual_delay = (exit_delay.value() == 0.0) ? 1.0 : exit_delay.value();
}
```

---

### 2. Update Editor Constructor (src/editor.h, src/editor.cpp)

**src/editor.h** - Change member variable:
```cpp
// Current:
bool exit_immediately_{false};

// Proposed:
double exit_delay_{0.0};  // 0.0 = no exit delay (normal operation)
```

**src/editor.cpp** - Update constructor signature:
```cpp
// Current:
editor::editor(bool debug_mode, const std::string &debug_string, 
               const std::vector<std::string> &filenames, bool exit_immediately,
               bool no_lsp)
    : exit_immediately_(exit_immediately), ...

// Proposed:
editor::editor(bool debug_mode, const std::string &debug_string, 
               const std::vector<std::string> &filenames, double exit_delay,
               bool no_lsp)
    : exit_delay_(exit_delay), ...
```

---

### 3. Update Main Loop (src/editor.cpp)

**Current**:
```cpp
if (exit_immediately_) {
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count() > 1000) {
        is_running_ = false;
    }
}
```

**Proposed**:
```cpp
if (exit_delay_ > 0.0) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
    auto delay_ms = static_cast<long long>(exit_delay_ * 1000.0);
    
    if (elapsed_ms > delay_ms) {
        is_running_ = false;
    }
}
```

**Rationale**:
- Supports sub-second precision (e.g., `0.5` seconds = 500ms)
- Maintains backward compatibility (default 1.0 second)
- Clear logic: `exit_delay_ > 0.0` means "exit after delay"

---

### 4. Update Main.cpp Call Site

**Current**:
```cpp
editor main_editor(debug_mode, debug_string, filenames, exit_immediately, no_lsp);
```

**Proposed**:
```cpp
double actual_delay = exit_delay.value_or(0.0);  // 0.0 if flag not provided
editor main_editor(debug_mode, debug_string, filenames, actual_delay, no_lsp);
```

---

### 5. Update Documentation

**docs/todo.md** - Mark as completed:
```markdown
## 26-05-2026
- implemented configurable --exit-immediately delay (optional seconds argument)
```

**docs/heaptrack.md** - Add example:
```markdown
# Exit after 5 seconds to allow heaptrack to capture more data
heaptrack build/turbostar src/document.cpp --exit-immediately=5
```

**docs/release-checklist.md** - Add example:
```bash
# Exit after 2 seconds for more thorough leak detection
valgrind --leak-check=full --show-leak-kinds=all build/turbostar --exit-immediately=2
```

---

## Edge Cases to Consider

### 1. Zero Delay (`--exit-immediately=0`)

**Question**: Should `0` mean "exit immediately" (no delay) or be treated as "use default (1 second)"?

**Proposal**: Treat `0` as "exit immediately" (no delay). This is intuitive:
- `--exit-immediately` or `--exit-immediately=1` → 1 second delay (default)
- `--exit-immediately=0` → exit as soon as possible (no artificial delay)

**Implementation**:
```cpp
// In main.cpp:
double actual_delay = exit_delay.value_or(0.0);  // 0.0 if flag not provided
// If user explicitly passes 0, actual_delay will be 0.0
```

**Alternative**: If you want `0` to mean "use default":
```cpp
double actual_delay = exit_delay.value_or(0.0);
if (exit_delay.has_value() && exit_delay.value() == 0.0) {
    actual_delay = 1.0;  // Treat 0 as "use default"
}
```

### 2. Negative Values

**Current CLI11 Range Check**: `CLI::Range(0.0, 3600.0)` will reject negative values with an error:
```
Error: --exit-immediately: ' -5 ' is not in range [0.0, 3600.0]
```

This is the desired behavior.

### 3. Very Large Values

**Current CLI11 Range Check**: `CLI::Range(0.0, 3600.0)` limits to 1 hour max.

**Rationale**: A 1-hour delay is already excessive for testing purposes. If someone needs longer, they can use `sleep` in the shell:
```bash
build/turbostar --exit-immediately=1 & sleep 3500 && kill %1
```

### 4. Sub-Second Precision

**Proposal**: Support sub-second delays (e.g., `0.1`, `0.5`) for fine-grained testing.

**Implementation**: Use `double` for the delay value and convert to milliseconds:
```cpp
auto delay_ms = static_cast<long long>(exit_delay_ * 1000.0);
```

**Example**:
- `--exit-immediately=0.1` → 100ms delay
- `--exit-immediately=0.5` → 500ms delay

### 5. Invalid Input

**Examples**:
- `--exit-immediately=abc` → CLI11 error: "Could not convert: abc"
- `--exit-immediately=-1` → CLI11 error: "is not in range [0.0, 3600.0]"
- `--exit-immediately=9999` → CLI11 error: "is not in range [0.0, 3600.0]"

CLI11 handles these gracefully with error messages.

---

## Testing Strategy

### Unit Tests

```cpp
// Test 1: Default delay (flag not provided)
editor ed(false, "", {}, 0.0, false);
// exit_delay_ should be 0.0

// Test 2: Default delay (flag provided without value)
// This depends on CLI11 behavior - may need to test via integration

// Test 3: Custom delay
editor ed(false, "", {}, 5.0, false);
// exit_delay_ should be 5.0

// Test 4: Sub-second delay
editor ed(false, "", {}, 0.5, false);
// exit_delay_ should be 0.5

// Test 5: Zero delay
editor ed(false, "", {}, 0.0, false);
// exit_delay_ should be 0.0
```

### Integration Tests

```bash
# Test 1: No flag - should run normally (manual test)
build/turbostar src/document.cpp
# (Press Ctrl-Q to exit)

# Test 2: Default delay - should exit after 1 second
time build/turbostar src/document.cpp --exit-immediately
# Expected: ~1 second

# Test 3: Custom delay - should exit after 5 seconds
time build/turbostar src/document.cpp --exit-immediately=5
# Expected: ~5 seconds

# Test 4: Zero delay - should exit as fast as possible
time build/turbostar src/document.cpp --exit-immediately=0
# Expected: <1 second (startup time only)

# Test 5: Sub-second delay - should exit after 500ms
time build/turbostar src/document.cpp --exit-immediately=0.5
# Expected: ~0.5 seconds

# Test 6: Invalid input - should show error
build/turbostar --exit-immediately=abc
# Expected: CLI11 error message

# Test 7: Out of range - should show error
build/turbostar --exit-immediately=9999
# Expected: CLI11 range error
```

### Memory Leak Testing

```bash
# With heaptrack (as documented)
heaptrack build/turbostar src/document.cpp --exit-immediately=5

# With valgrind (as documented)
valgrind --leak-check=full --show-leak-kinds=all build/turbostar --exit-immediately=2
```

---

## Backward Compatibility

### Current Behavior

```bash
build/turbostar --exit-immediately  # Exits after 1 second
```

### New Behavior

```bash
build/turbostar --exit-immediately  # Exits after 1 second (default)
build/turbostar --exit-immediately=1  # Exits after 1 second (explicit)
build/turbostar --exit-immediately=5  # Exits after 5 seconds (new)
```

**Compatibility**: **Fully backward compatible**. Existing scripts using `--exit-immediately` will continue to work exactly as before (1 second delay).

---

## Alternative Designs

### Option 1: Separate Flags

```cpp
bool exit_immediately = false;
int exit_delay_seconds = 1;
app.add_flag("--exit-immediately", exit_immediately, "Exit after delay");
app.add_option("--exit-delay", exit_delay_seconds, "Delay in seconds")->default_val(1);
```

**Pros**: Clear separation of concerns
**Cons**: More verbose (`--exit-immediately --exit-delay=5`)

### Option 2: Separate Flags with Implied Default

```cpp
int exit_delay = 0;  // 0 = no exit delay (normal operation)
app.add_option("--exit-immediately", exit_delay, "Exit after N seconds (0=normal)")
   ->default_val(0)
   ->check(CLI::Range(0, 3600));
```

**Pros**: Single option, simple
**Cons**: Confusing semantics (what does `--exit-immediately=0` mean?)

### Option 3: Current Proposal (Recommended)

```cpp
std::optional<double> exit_delay;
app.add_option("--exit-immediately", exit_delay, "Exit after N seconds (default: 1)")
   ->check(CLI::Range(0.0, 3600.0));
```

**Pros**: Clean, intuitive, backward compatible, supports sub-second precision
**Cons**: Slightly more complex type (std::optional)

---

## Estimated Effort

| Task | Complexity | Time Estimate |
|------|------------|---------------|
| CLI11 option change | Low | 30 minutes |
| Editor constructor update | Low | 15 minutes |
| Main loop update | Low | 15 minutes |
| Documentation update | Low | 30 minutes |
| Testing | Medium | 1 hour |
| **Total** | | **~2.5 hours** |

---

## Risks and Mitigations

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| CLI11 `add_option` with `std::optional` doesn't work as expected | Low | Medium | Test thoroughly; fallback to `double` with special handling for 0 |
| Breaking existing scripts | Low | High | Maintain default 1-second delay; document change |
| Sub-second precision issues | Low | Low | Use `double` and convert to milliseconds; test with `0.1`, `0.5` |
| Integer overflow in delay calculation | Very Low | Low | Use `long long` for milliseconds; max 3600 seconds = 3.6M ms |

---

## Conclusion

**Recommendation**: Implement the proposed change using `std::optional<double>` with CLI11's `add_option`. This provides:

1. **Backward compatibility** - Existing `--exit-immediately` usage unchanged
2. **Flexibility** - Support for custom delays (0 to 3600 seconds)
3. **Precision** - Sub-second delays supported (e.g., `0.5` seconds)
4. **Validation** - CLI11 range checking prevents invalid input
5. **Clean API** - Intuitive syntax (`--exit-immediately=5`)

**Implementation Priority**: Medium-High  
**Justification**: Enables better testing workflows (memory leak detection, startup profiling) with minimal effort.

---

## Appendix: CLI11 Documentation References

- `add_option`: https://cliutils.github.io/CLI11/book/chapters/options.html
- `check(CLI::Range)`: https://cliutils.github.io/CLI11/book/chapters/validation.html
- `default_val`: https://cliutils.github.io/CLI11/book/chapters/options.html#default-values
- `std::optional`: https://cliutils.github.io/CLI11/book/chapters/app.html#optional-values
