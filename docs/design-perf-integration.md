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
│ Target Application Process (Launched with LD_PRELOAD=libturbocatch.so)       │
│                                                                             │
│   1. __attribute__((constructor))                                           │
│      ├── Check environment: TURBOSTAR_PROFILE == "1"                        │
│      └── If enabled: Call perf_event_open(pid=0, PERF_COUNT_HW_CPU_CYCLES)   │
│                                                                             │
│   2. Execution Sampling                                                     │
│      └── Ring buffer samples aggregated into in-memory IP hit count map      │
│                                                                             │
│   3. __attribute__((destructor)) / atexit()                                 │
│      └── Write aggregated IP histogram to tmp://perf_<pid>.json              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 2.2 Security & Permissions (`perf_event_paranoid`)
By profiling self (`pid = 0`), `libturbocatch.so` operates under standard user permissions and avoids restrictive `/proc/sys/kernel/perf_event_paranoid` policies that typically block cross-process profiling.

### 2.3 Opt-In Environment Control
- **Disabled (Default)**: When `TURBOSTAR_PROFILE` is unset or `0`, `libturbocatch.so` skips `perf_event_open(2)` initialization. Overhead is **0% CPU / 0% Memory** (only crash signal handlers remain active).
- **Enabled**: When `TURBOSTAR_PROFILE=1` (or `TURBOSTAR_PROFILE_HZ=1000`) is injected by `command_runner` or an agent tool call, profiling automatically engages for the run session.

### 2.4 Ring Buffer Strategy (Zero-Thread Demand-Paged Virtual Memory)
To eliminate background thread creation ("evil preload threads"), `fork()` synchronization hazards, and signal interruptions, `libturbocatch.so` leverages 64-bit Linux virtual address space demand-paging:

- **Large Virtual `mmap` Allocation**: `libturbocatch.so` maps a large power-of-two virtual memory ring buffer (e.g. 64 MB to 256 MB via `mmap(NULL, (1 + 2^n) * PAGE_SIZE, ...)`).
- **Demand-Paged Physical RAM**: On 64-bit Linux, virtual address allocation consumes 0 MB of physical RAM up front. Physical RAM pages are committed by the kernel demand-pager only as the kernel PMU fills sample records.
- **Zero Runtime Code Execution**: During execution, `libturbocatch.so` executes **zero code, zero locks, zero polling threads, and zero signal handlers**. The Linux kernel writes raw sample structs directly to the mapped memory ring.
- **Single-Pass Exit Drain**: Upon target process termination or crash, `libturbocatch.so` inspects the kernel ring head pointer (`data_head`), performs a single fast linear pass over committed pages to build the IP histogram, and flushes `tmp://perf_<pid>.json`.

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

### 4.1 Editor UI Visualizations
- **Line Gutter Percentages**: Displays percentage hits in the editor line-number margin (e.g. ` 14.2% │ for (int i = 0; i < n; ++i)`).
- **Ncurses Heatmap Gradient**:
  - **Top 1% Hot Lines**: Vibrant red background / text.
  - **Top 5% Hot Lines**: Warm orange/yellow text.
  - **Top 20% Hot Lines**: Subtle cyan highlight.

### 4.2 LLM Agent Tools
- `agent_get_profile_summary`: Returns top N hot functions by cycle percentage.
- `agent_get_profile_details`: Returns line-by-line percentage breakdowns for a target function or source file.

---

## 5. Future Considerations
- **Call-Graph Stack Traces**: Optional unwinding via DWARF/frame pointers for flamegraph generation in future iterations.
- **Memory Allocation Profiling**: Tracking heap allocations via `brk`/`mmap` tracepoints if required.
