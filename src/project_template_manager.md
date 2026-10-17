# Project Template Manager (`project_template_manager`)

## Goals
The `project_template_manager` class (`src/project_template_manager.h`, `src/project_template_manager.cpp`) provides scaffolding and instantiation for newly created software projects in Turbostar.
Its primary responsibilities include:
1. **Template Discovery**: Enumerating available built-in templates (such as Meson C++, CMake C++, Meson C, CMake C, Cargo Rust, and Python pyproject.toml) embedded via `project_templates_embedded.h`.
2. **Variable Substitution**: Replacing project variable placeholders (`{{PROJECT_NAME}}`, `{{PROJECT_NAME_LOWER}}`, `{{EXECUTABLE_NAME}}`, `{{LANGUAGE_STANDARD}}`, `{{YEAR}}`) during instantiation.
3. **Language Standard Overrides**: Resolving file overrides with version suffixes (e.g. `.C++23`, `.C++20`, `.C++17`, `.C17`) matching user options.
4. **Repository Initialization**: Setting up the project directory and initializing git repositories if requested.

## Architecture and Constraints
- **Embedded Templates**: Templates are precompiled into binary memory structures using python build scripts at meson configure time.
- **Case Normalization**: Project identifier transformations (such as `{{PROJECT_NAME_LOWER}}`) use `utf8::to_lower` to ensure ASCII-safe conversion without signed `<cctype>` undefined behavior or locale anomalies.
- **Directory Safety**: Validates target directory emptiness before file generation to avoid overwriting existing repositories.

## Lessons Learned
- **ASCII Identifier Case Folding**: Using `utf8::to_lower` ensures project identifiers and CMake / Meson variable substitutions avoid locale-sensitive behavior (such as the Turkish I problem) while remaining fast and zero-overhead.
