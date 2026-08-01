#pragma once
#include <unordered_map>
#include <string>
#include <string_view>

namespace turbostar {

inline const std::unordered_map<std::string, std::string_view> &get_embedded_system_docs()
{
    static const std::unordered_map<std::string, std::string_view> docs = {
        {"workflows/code_review.md", R"raw_embed(# Code Review Subagent Workflow

## Guidelines
- **File Slicing**: Do not review more than 10 files or 1,500 lines of code in a single subagent call. Group larger file lists logically and invoke `perform_code_review` separately for each group.
- **Checklists & Instructions**: Provide overall context in `instructions` and supply a checklist of specific review items in the `todos` vector.
- **Post-Review**: Call `list_code_review_items` to get a concise summary table of active findings, and `get_code_review_item` to retrieve details.
- **Resolution**: Use `resolve_code_review_item` when issues are addressed or ruled out.
)raw_embed"},
        {"workflows/plan_mode.md", R"raw_embed(# Plan Mode Workflow Guidelines

## Guidelines
- **Mandatory for Complex Tasks**: For complex multi-file tasks or non-trivial refactoring, call `enter_plan_mode` before making edits.
- **Read-Only Exploration**: While in plan mode, only read-only tools (and scratch `tmp://` writes) are available.
- **Plan Presentation**: Formulate a clear, step-by-step plan, present it to the user, and call `exit_plan_mode` to begin implementation.
)raw_embed"},
        {"languages/cpp23.md", R"raw_embed(# Modern C++23 Coding Guidelines

## Core Principles
- **Standard Version**: TurboStar uses C++23. Use modern language features (`std::format`, `std::string_view`, `std::span`, `constexpr`, `noexcept`, structured bindings) whenever appropriate.
- **Memory & Resource Management**: Follow RAII strictly. Avoid raw `new` and `delete`; favor `std::make_unique` and `std::make_shared`.
- **String & Sequence Parameters**: Prefer `std::string_view` for read-only string parameters over `const std::string &`. Prefer `std::span<const T>` for read-only contiguous sequence parameters over `const std::vector<T> &` to avoid unnecessary allocations.
- **Formatting**: Prefer `std::format` over raw C string concatenations or `sprintf`.
- **Include Paths**: All `#include ""` directives must be relative to the `src/` directory (e.g., `#include "fs_utils.h"` instead of relative `../../` paths).
- **Include Guards**: Use `#pragma once` for all header files.

## Concurrency & Mutexes
- **Mutex Documentation**: When declaring a mutex in a header file, you MUST add a comment block immediately preceding the declaration explaining:
  1. What specific member data or resources the mutex protects.
  2. The general locking rules, lifecycle, or ordering guidelines associated with it.

## Unit Test Guidelines
- **Test Isolation**: Every unit test MUST isolate the `HOME` environment variable to a temporary directory to prevent race conditions during parallel test execution. Call `test_watchdog::setup_watchdog()` at the start of `main()`.
)raw_embed"},
        {"languages/python311.md", R"raw_embed(# Python 3.11 Coding Guidelines

## Core Principles
- **Language Version**: Python 3.11+. Use modern type hinting (`type_a | type_b`, `list[str]`, `dict[str, int]`).
- **Security Validation**: Code executed or created must pass `bandit` security checks (no `shell=True` in subprocess calls, no unvalidated `eval()`).
- **Virtual Environments**: Prefer running Python scripts within isolated environments or VFS `tmp://` scratch spaces.
)raw_embed"},
    };
    return docs;
}

} // namespace turbostar
