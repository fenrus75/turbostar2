# Profiling Memory with Heaptrack

`heaptrack` is a powerful tool for analyzing memory allocations, finding memory leaks, and identifying allocation hotspots (excessive temporary allocations) in Turbostar. 

Future agents should use this tool when diagnosing performance issues, "Out of Memory" (OOM) errors, or slow startup times.

## Workflow for Agents

### 1. Run the Profiler
To profile the application during a quick startup/shutdown cycle, run Turbostar through `heaptrack` and use the `--exit-immediately` flag (which causes the editor to quit right after initialization).

You can pass a dummy file to simulate a normal load:
```bash
heaptrack build/turbostar src/document.cpp --exit-immediately
```

### 2. Locate the Output File
The command above will output the path to the generated data file, which typically looks like this:
```text
heaptrack output will be written to "/home/.../heaptrack.turbostar.12345.gz"
```

### 3. Analyze the Data
Use the `heaptrack --analyze` command on the generated `.gz` file to extract the backtraces and allocation statistics.

```bash
heaptrack --analyze "heaptrack.turbostar.12345.gz"
```

Because the output can be very large, you may want to pipe it through `head` or `grep` depending on what you are looking for:
```bash
# Look at the top allocation hotspots
heaptrack --analyze "heaptrack.turbostar.12345.gz" | head -n 100
```

## Interpreting the Results

### Allocation Hotspots (`MOST CALLS TO ALLOCATION FUNCTIONS`)
This section shows where the application is doing the most work allocating memory. Look for high call counts (e.g., `25000 calls to allocation functions...`). 
*   **Common culprits:** `std::string` allocations in loops, recursive filesystem walks, or tight parsing loops.
*   **Fixes:** Switch to `std::string_view`, reuse string buffers, or pre-allocate vectors (`.reserve()`).

### Memory Leaks (`total memory leaked`)
`heaptrack` will report memory that was allocated but never freed before the application exited.

**⚠️ WARNING: False Positives**
You will often see leaks originating from:
*   `CRYPTO_malloc` / `OPENSSL_init_crypto`
*   `_dl_init` (the dynamic linker)
*   `_nl_make_l10nflist` (locale initializations)

These are standard, one-time initializations performed by external C libraries (like OpenSSL used in `httplib`) or the OS. They intentionally do not free their memory at shutdown because the OS reclaims it anyway. **Do not attempt to "fix" these leaks.** Focus only on leaks originating from `src/` (Turbostar's own codebase).
