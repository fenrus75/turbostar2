# `verilog::verilog_sim_build_cache`

## Goals & Constraints
- **Layer 1 (Build Fingerprint Cache + In-Flight Deduplication)**:
  - Scopes each testbench's inputs (`collect_tb_sources`) to its instantiated module hierarchy, package/interface files, and relevant `formal/*.sv` `bind` files so unrelated RTL edits do not force leaf testbenches to recompile.
  - Computes an FNV-1a signature over `(top_mod, coverage, formal, sorted(canonical_path, size, mtime_ns))` persisted in `<mdir>/.turbostar_build.sig`. If `V<top>` already matches, Verilator is skipped in 0ms.
  - Serializes/deduplicates concurrent builds targeting the same `--Mdir` via `in_flight_builds_` futures so `verilog_simulate()` joins an in-flight AOT pre-compile instead of launching a duplicate Verilator process.
- **Layer 2 (Session-Gated Debounced AOT Pre-Compiler)**:
  - Stays dormant until `record_simulation_session()` is called by `verilog_simulate` (and never activates in one-shot `turbostar-verilog` CLI mode).
  - Debounces `.sv`/`.v`/`.svh`/`.vh` edits for 1.5s, works through all affected `quick` testbenches (direct/leaf testbenches first, then composite ones) 1 at a time using at most half of available CPU cores (`std::max(1, hw_threads / 2)`), aborting immediately between testbenches if a new edit arrives.
  - Cleanly stops and joins its `std::jthread` via `shutdown()` on `plugin_unload()` and destruction.

## Lessons Learned
- Never hold `in_flight_mutex_` while waiting on a `std::shared_future` or invoking `verilator`, and never hold `aot_mutex_` while scanning disk or compiling.
