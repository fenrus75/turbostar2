# Performance Profiling Subsystem Design (`perf_event_open` & `libturbocatch.so`)

## 1. Overview & Objectives

This document defines the architectural design for Turbostar's developer-mode performance profiling subsystem. 

The primary objective is to provide zero-dependency, highly reliable CPU performance insights directly within the Turbostar editor UI and to the LLM agent.

### Key Goals
- **Zero External Tool Dependencies**: Use Linux kernel `perf_event_open(2)` system calls directly instead of relying on the external `/usr/bin/perf` binary (avoiding Linux kernel version mismatch issues across distros).
- **KISS Principle (Flat Cycle Counts)**: Focus strictly on flat CPU hardware cycles (`PERF_COUNT_HW_CPU_CYCLES`) per function and per line, avoiding complex call-graph stack unwinding in initial versions.
- **Unified Preload Infrastructure**: Integrate profiling hooks into Turbostar's existing `libturbocatch.so` `LD_PRELOAD` library rather than introducing a second shared library.
- **High-Performance Symbol Resolution**: Solve the address-to-line translation bottleneck via in-memory IP histogramming combined with persistent streaming pipes to `eu-addr2line` / `libdw`.
- **Dual Presentation**: Expose hot-line visual gutter/frame color highlights in the editor window, and provide structured profiling tools (`agent_get_profile_summary`, `agent_get_profile_details`) to the LLM agent.

---

## 2. Architecture & Lifecycle

### 2.1 In-Process Self-Profiling (`pid = 0`)
Profiling is performed from within the target application process via `libturbocatch.so`:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│ 1. TARGET PROCESS (LD_PRELOAD=libturbocatch.so - Pure C, Zero libstdc++)     │
│    ├── __attribute__((constructor)): perf_event_open(pid=0)                 │
│    ├── During Exit: Drain mmap ring through static direct-mapped C cache     │
│    ├── On Cache Collision: Direct write(fd, &slot, sizeof(slot)) to disk      │
│    └── Output: /tmp/turbostar_perf_samples_<pid>.dat & maps file            │
└─────────────────────────────────────────────────────────────────────────────┘
                                     │
                                     ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ 2. TURBOSTAR HOST / POST-PROCESSOR (C++23)                                   │
│    ├── Reads raw sample dat & maps file from /tmp                           │
│    ├── Merges partial counts via std::unordered_map                         │
│    ├── Invokes turbostar::address_lookup::resolve_addresses(unique_ips)      │
│    └── Cleans up raw /tmp files and renders UI / LLM Agent tools             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 2.2 Security & Permissions (`perf_event_paranoid`)
By profiling self (`pid = 0`), `libturbocatch.so` operates under standard user permissions and avoids restrictive `/proc/sys/kernel/perf_event_paranoid` policies that typically block cross-process profiling.

### 2.3 Opt-In Environment Control
- **Disabled (Default)**: When `TURBOSTAR_PROFILE` is unset or `0`, `libturbocatch.so` skips `perf_event_open(2)` initialization. Overhead is **0% CPU / 0% Memory** (only crash signal handlers remain active).
- **Enabled**: When `TURBOSTAR_PROFILE=1` (or `TURBOSTAR_PROFILE_HZ=1000`) is injected by `command_runner` or an agent tool call, profiling automatically engages for the run session.

### 2.4 Pure C Opportunistic Cache & Zero-Allocation Disk Flushing
To keep `libturbocatch.so` written in **100% pure C** with zero `libstdc++.so` or `malloc` dependency:

- **Large Virtual `mmap` Allocation**: `libturbocatch.so` maps a 64 MB to 256 MB virtual memory ring buffer (`mmap(NULL, (1 + 2^n) * PAGE_SIZE, ...)`), consuming 0 MB of physical RAM up front via 64-bit Linux demand-paging.
- **Zero Runtime Code Execution**: During execution, the kernel PMU writes raw sample structs directly into the ring buffer. Zero background threads or signal handlers are used.
- **Static In-Preload Cache**: A static direct-mapped array `static struct sample_slot cache[2048];` is declared in BSS memory inside `libturbocatch.so`.
- **Opportunistic Data Reduction**: When draining the ring buffer on process exit, `libturbocatch.so` maps IPs via `idx = (ip ^ (ip >> 12)) & 2047`.
  - **Hit**: Increment counter in place (achieves ~95–99% reduction due to code locality).
  - **Collision**: Write evicted slot directly to disk using `write(fd, &slot, sizeof(slot))` and update slot.
- **Disk Outputs & Cleanup**: Output files are written to `/tmp/turbostar_perf_samples_<pid>.dat` and `/tmp/turbostar_perf_maps_<pid>.txt` using the exact same path resolution mechanism as crash dump files. Host Turbostar (C++) post-processes the raw files, resolves symbols via `turbostar::address_lookup`, saves the final profile result, and deletes the temporary `/tmp` raw files.

---

## 3. Symbol & Line Resolution Pipeline

To prevent performance bottlenecks when converting thousands of raw sample addresses to source code line numbers:

```
[Raw 100,000 Samples] ──> [In-Memory IP Histogram: ~200 Unique IPs]
                                     │
                                     ▼
                      [Persistent eu-addr2line STDIN Pipe]
                                     │
                                     ▼
                 [Mapped: file.cpp:line_number -> % cycles]
```

1. **Histogram Reduction**: 100,000 raw samples collapse into ~150–300 unique Instruction Pointer (IP) addresses in an `unordered_map<uintptr_t, uint64_t>`.
2. **Binary Offset Adjustment**: Base load addresses are adjusted for Position-Independent Executables (PIE) via `/proc/<pid>/maps` or `dl_iterate_phdr`.
3. **Batch Symbol Translation**: Turbostar pipes the ~200 unique IP addresses to a persistent `eu-addr2line` daemon over STDIN in a single batch (< 5ms total execution time).
4. **Percentage Assignment**: Line percentages are calculated as:
   $$\text{Line \%} = \frac{\text{Line Sample Count}}{\text{Total Session Samples}} \times 100$$

---

## 4. UI & AI Agent Integration

### 4.1 Editor UI Visualizations & Line Tracking

#### Left Window Border Heatmap Indicators
To visualize performance bottlenecks without stealing line-number gutter width or shrinking editor text area:
- **Dithered Density Blocks & Color Gradient**: For lines with performance samples ($\ge 1\%$ global cycles), the default double vertical window frame character `║` on the left border of the editor window is replaced with progressively thicker dithered block glyphs and distinct color pairs:
  - $\ge 1\%$ global samples: **Light Shade `░`** in **Bright Cyan** (`COLOR_PAIR(32)`)
  - $\ge 10\%$ global samples: **Medium Shade `▒`** in **Bright Yellow** (`COLOR_PAIR(21)`)
  - $\ge 50\%$ global samples: **Solid Block `█`** in **Bright Red** (`COLOR_PAIR(31)`)

#### Status Bar Detail Messages
When the cursor moves onto any line containing performance samples (significant or not):
- The editor issues `set_status_message(payload, status_priorities::HOVER)` to display detailed sample statistics in the status bar:
  `Perf: 60.4% (3080 samples)`
- Moving the cursor to a line with zero performance samples automatically clears the status message via `clear_status_message(status_priorities::HOVER)`.

#### Dynamic Edit Tracking via `document_listener`
To keep line sample associations accurate as the user or LLM agent edits the document:
- **Listener Registration**: `perf_manager` registers a `document_listener` callback with the active `document` instance via `document::add_listener()`.
- **Line Insertions (`on_line_inserted(filename, y)`)**: Sample line numbers at or below the insertion point (`line_number >= y + 1`) shift down by +1 (`line_number++`).
- **Line Deletions (`on_line_deleted(filename, y)`)**: Samples on the deleted line (`line_number == y + 1`) are marked stale, while samples below shift up by -1 (`line_number--`).
- **Validity Limit & Reset**: Each document tracks a `perf_modification_count`. After $\ge 20$ line modifications, the profile data for that document is marked invalid (`perf_valid = false`), gracefully hiding border highlights until a fresh profiling run is executed.

### 4.2 "Go to Next Hotspot" Navigation (`F7` & Menu Item)

To allow developers to quickly cycle through performance bottlenecks across open and closed files:

#### Menu Item & Shortcut Binding
- **Menu Location**: Added as the 4th item under the **Run** menu: `Go to next hotspot`
- **Shortcut Binding**: Keyed to **`F7`** (and `event_type::go_to_next_hotspot`).
- **Menu Item State**: Disabled / grayed-out when no active performance profile data exists or when `top_lines` is empty. Enabled as soon as profile data is parsed.

#### Stepping Logic & Ranking
1. **Ranked Cycle Order**: Navigation cycles through `active_report_.top_lines` in **descending order of CPU cycle percentage** (#1 hottest line $\rightarrow$ #2 $\rightarrow$ #3 $\rightarrow$ ...).
2. **Cursor Location Matching**:
   - **Cursor Not on Hotspot**: If the current active window cursor is NOT on a line matching any entry in `top_lines`, `F7` jumps directly to **Hotspot #1** (the global #1 bottleneck line).
   - **Cursor on Hotspot #K**: If the cursor IS currently located on Hotspot #K in `top_lines`, `F7` advances to **Hotspot #(K + 1)**.
   - **End-of-List Wrapping**: Stepping past the last hotspot wraps back to **Hotspot #1** and displays a status bar notification: `Hotspot wrap: back to #1 bottleneck`.

#### Window Activation, File Opening & Column Positioning
1. **File Window Activation**:
   - If the target hotspot file is **already open** in an editor window, that window is brought into focus (`set_active(true)`).
   - If the file is **NOT open**, it is automatically opened in a new editor window.
2. **Cursor & View Placement**:
   - The cursor moves to line `line_number - 1`.
   - **Opportunistic Column Position**: If `addr2line` provided a column number (`column_number > 0`), the cursor moves to `column_number - 1`; otherwise it defaults to column `0`.
   - **Auto-Centering**: The editor centers the window view vertically on the target line (`top_line_ = std::max(0, line_number - content_height / 2)`).

### 4.3 Multi-Run Profile Storage & Comparison via `run_id`

To enable LLM agents to compare "before" vs. "after" optimization benchmarks across multiple runs:
- **In-Memory Run Dictionary**: `perf_manager` maintains a dictionary `saved_reports_[run_id]` mapping string IDs (e.g. `"run_1"`, `"run_2"`, `"editor"`) to resolved `perf_profile_report` objects.
- **Exact `run_id` Alignment**: Tool queries and background sessions use the exact string execution ID returned by `agent_start_app` (or numeric string format `"run_N"`). Re-running an app session under an existing `run_id` automatically overwrites that report.
- **Tool Schema Parameters**: `agent_get_profile_summary` and `agent_get_profile_details` accept an optional `run_id` argument (string or int in JSON). Omitting `run_id` or passing `"latest"` returns the active profile for editor visualization.

---

## 5. Future Considerations
- **Call-Graph Stack Traces**: Optional unwinding via DWARF/frame pointers for flamegraph generation in future iterations.
- **Memory Allocation Profiling**: Tracking heap allocations via `brk`/`mmap` tracepoints if required.
