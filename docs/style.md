# PowerTOP Coding Style Guide

This document outlines the coding style and conventions used in the PowerTOP project, deduced from the existing codebase in the `src/` directory.

## 1. Indentation and Formatting

### 1.1 Indentation
*   **Tabs only**: Use tabs for indentation.
*   **Tab Width**: Tabs should be treated as 8 spaces wide.
*   **No trailing whitespace**: Lines should not end with whitespace.
*   **Automated Formatting**: Project formatting rules are encoded in the `.clang-format` file in the root directory. Developers should use `clang-format` to maintain consistency.

### 1.2 Braces and Control Flow
*   **K&R Style (Linux Kernel Variant)**:
    *   For `if`, `while`, `for`, `switch`, the opening brace `{` is on the same line as the statement.
    *   For function definitions, the opening brace `{` is on a new line.
    *   The closing brace `}` is on its own line, except for `else` which is placed between braces: `} else {`.

```cpp
void function_name()
{
	if (condition) {
		// code
	} else {
		// code
	}

	while (condition) {
		// code
	}
}
```

### 1.3 Switch Statements
*   Align `case` labels with the `switch` statement.
*   Indent the code within each `case`.

```cpp
switch (value) {
case 1:
	do_something();
	break;
default:
	break;
}
```

### 1.4 ternary operators
Do not use ternary operators, use discrete if statements always.
Exception: in C++ style << statements ternary operators are ok as otherwise
it may get unwieldy

Example of the exception (allowed case):
```c++
cout << (auto_open_error_files_ ? "true" : "false") << "\n";
```

## 2. Naming Conventions

### 2.1 Files
*   Use lowercase and snake_case for filenames (e.g., `devlist.cpp`, `report-maker.cpp`).
*   C++ source files use `.cpp` extension.
*   Header files use `.h` extension.

### 2.2 Variables and Functions
*   **Variables**: Use `snake_case` (e.g., `debug_learning`, `time_out`).
*   **Functions**: Use `snake_case` (e.g., `print_usage`, `do_sleep`).

### 2.3 Classes and Structs
*   **Classes**: Use `snake_case` (e.g., `device`, `abstract_cpu`).
*   **Structs**: Use `snake_case` (e.g., `idle_state`, `frequency`).
*   **Methods**: Use `snake_case` (e.g., `start_measurement`, `human_name`).

### 2.4 Macros and Constants
*   Use `UPPER_SNAKE_CASE` (e.g., `DEBUGFS_MAGIC`, `NR_OPEN_DEF`, `OPT_AUTO_TUNE`).

## 3. Comments

### 3.1 File Header
*   Every file must start with the standard Copyright and GPL license header.

```cpp
/*
 * Copyright 2026, Arjan van de Ven
 *
 * This file is part of Turbostar
 * ... (GPL License text)
 * Authors:
 *	Name <email>
 */
```

### 3.2 Block and Inline Comments
*   Use C-style `/* ... */` for block comments and multi-line explanations.
*   C++ style `//` is acceptable for short, single-line comments.

## 4. C++ Practices

### 4.1 Header Guards
*   Use `#pragma once`

### 4.2 Namespaces
*   Avoid `using namespace std;`.
*   Always use the `std::` prefix for standard library elements (e.g., `std::string`, `std::vector`, `std::ifstream`).

### 4.3 Strings
*   Prefer `std::string` over C-style strings (`char *`) or fixed-size buffers, unless interfacing with C-only libraries.
*   Always use the fully qualified `std::string` type.
*   **Formatting**: Use `std::format` for internal strings and `pt_format` for user-facing (translated) strings.
    *   `pt_format(_("..."), args...)` is required for gettext-translated strings to ensure compatibility with runtime format strings in C++20.
    *   Do not use `std::vformat` or `std::make_format_args` directly; use the `pt_format` helper instead.
    *   Example: `humanname = pt_format(_("USB device: {}"), device_name);`
*   **Logging**: Do not use `+` string concatenation or `std::to_string` inside `event_logger::get_instance().log(...)`. Use the variadic overload of `event_logger::get_instance().log(...)` which formats the message automatically like `std::format`.
    *   Example: `event_logger::get_instance().log("Wrote Linux Kernel .clang-format to {}", format_file.string());`


### 4.4 Includes
*   Order: Standard library headers first, followed by project-specific headers.
*   Project headers should be included using quotes (e.g., `#include "lib.h"`).


# C++ STL 

- use std::clamp for clamping values
- use std::format whenever possible
