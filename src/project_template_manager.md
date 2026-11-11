# Project Template Manager (`project_template_manager`)

## Goals
The `project_template_manager` class (`src/project_template_manager.h`, `src/project_template_manager.cpp`) provides scaffolding and instantiation for newly created software projects in Turbostar.
Its primary responsibilities include:
1. **Template Discovery**: Enumerating available built-in templates (such as Meson C++, CMake C++, Meson C, CMake C, Cargo Rust, and Python pyproject.toml) embedded via `project_templates_embedded.h`.
2. **Variable Substitution**: Replacing project variable placeholders (`{{PROJECT_NAME}}`, `{{PROJECT_NAME_LOWER}}`, `{{EXECUTABLE_NAME}}`, `{{LANGUAGE_STANDARD}}`, `{{YEAR}}`) during instantiation.
3. **Language Standard Overrides**: Resolving file overrides with version suffixes (e.g. `.C++23`, `.C++20`, `.C++17`, `.C17`) matching user options.
4. **Repository Initialization**: Setting up the project directory and initializing git repositories if requested.
5. **Canonical Language Presets**: Exposing centralized language definitions, mnemonic hotkeys, standards, and build system presets (`get_language_presets()` and `find_language_preset()`) shared between the New Project wizard and the Workspace Preferences dialog.

## Architecture and Constraints
- **Embedded Templates**: Templates are precompiled into binary memory structures using python build scripts at meson configure time.
- **Canonical Presets**: `language_preset_definition` serves as the single source of truth for supported project languages (`C++`, `C`, `Python`, `Rust`, `SystemVerilog`, `Other`), their respective version presets, build system options, and hotkeys.
- **Case Normalization**: Project identifier transformations (such as `{{PROJECT_NAME_LOWER}}`) use `utf8::to_lower` to ensure ASCII-safe conversion without signed `<cctype>` undefined behavior or locale anomalies.
- **Directory Safety**: Validates target directory emptiness before file generation to avoid overwriting existing repositories.

## Lessons Learned
- **Single Source of Truth for Language Options**: Decoupling language and standard version presets from UI containers into `project_template_manager` prevents configuration drift between the New Project wizard and the Preferences dialog.
- **ASCII Identifier Case Folding**: Using `utf8::to_lower` ensures project identifiers and CMake / Meson variable substitutions avoid locale-sensitive behavior (such as the Turkish I problem) while remaining fast and zero-overhead.
- **Unified SystemVerilog Template (`systemverilog`)**: Consolidating separate simulation (`verilator_sv`) and synthesis (`fpga_ice40`) templates into a single `systemverilog` template without a static `meson.build` aligns new projects directly with the `verilog` plugin tools (`verilog_compile`, `verilog_simulate`, `turbostar.json`), avoiding out-of-sync source file lists when modules or testbenches are added.
