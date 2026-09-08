# @@PROJECT_NAME@@ Development Guidelines

## Build & Test Instructions
- **Setup Build Directory**: `meson setup build`
- **Compile / Lint RTL**: `meson compile -C build` (or `fs_compile_project`)
- **Run Verification Tests**: `meson test -C build` (or `fs_run_tests`)
- **Clean Build Directory**: `ninja -C build clean`

## Code Conventions
- Language Standard: @@LANGUAGE_STD@@
- Use `logic` for all nets and variables instead of `wire`/`reg`.
- Use `always_comb` for combinational logic and `always_ff @(posedge clk or negedge rst_n)` for flip-flops.
- Always separate module interfaces into clean input/output signal definitions.
