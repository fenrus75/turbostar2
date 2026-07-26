# @@PROJECT_NAME@@ Development Guidelines

## Build & Test Instructions
- **Configure**: `meson setup build`
- **Compile**: `ninja -C build`
- **Run**: `./build/@@EXECUTABLE_NAME@@`
- **Test**: `meson test -C build`

## Code Conventions
- Language Standard: @@LANGUAGE_STD@@
- Prefer `#pragma once` for include guards.
- Follow RAII principles strictly, avoid "new" and "delete" and use std::make_shared and std::make_unique instead
