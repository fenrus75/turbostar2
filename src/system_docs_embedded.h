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
        {"workflows/crash_analysis.md", R"raw_embed(# Crash & Bug Analysis Protocol (What – How – Where)

When analyzing a crash report, log failure, core dump, or test failure, you **MUST** execute the 3-step **What – How – Where** analysis protocol below.

---

## Step 1: WHAT (Crash / Failure Identification)
Identify the precise failure condition and location from the failure report and source code.
- **Objective**: Determine the exact line, symbol, and invalid state that triggered the failure (e.g., `[editor_events.cpp:142] Pointer 'doc' was nullptr during dereference`).
- **Output**: Formulate a concise 1–2 sentence "What" summary.

---

## Step 2: HOW (Root-Cause Tracing)
Trace backwards through execution paths to determine *why* the invalid state occurred. Repeat the "How" query recursively (up to 3–4 levels) until reaching the true root cause.

Classify the analysis under one of these categories:
- **A. Positive How (Explicit Assignment)**: An explicit execution path assigned an invalid value (e.g., `nullptr`, invalid index, out-of-bounds size). Trace the assignment chain step-by-step with Q&A queries.
- **B. Negative How (Missing Initialization / Unhandled Path)**: The state was never initialized or assigned a valid value. Determine why normal initialization/assignment code path was bypassed.
- **C. Unknowable How (Insufficient Data)**: Do **NOT** write a speculative fix. Improve observability by adding structured logging, assertions, or telemetry at key decision points so future runs capture complete state.
- **D. Testcase Verification**: Create a minimal testcase that reproduces the root-cause state. Verify that the testcase **FAILS** prior to applying the fix and **PASSES** once the fix is applied.

---

## Step 3: WHERE (Fix Location)
Determine the correct location in the codebase for the fix based on Step 2.
1. **Root-Cause Fix (Preferred)**: Place the fix at the origin site identified in Step 2.
2. **No Superficial Symptom Patches**: Avoid placing a simple NULL check or swallow-exception block at the crash site unless handling the condition locally is valid domain logic.
)raw_embed"},
        {"workflows/plan_mode.md", R"raw_embed(# Plan Mode Workflow Guidelines

## Guidelines
- **Mandatory for Complex Tasks**: For complex multi-file tasks or non-trivial refactoring, call `enter_plan_mode` before making edits.
- **Read-Only Exploration**: While in plan mode, only read-only tools (and scratch `tmp://` writes) are available.
- **Plan Presentation**: Formulate a clear, step-by-step plan, present it to the user, and call `exit_plan_mode` to begin implementation.
)raw_embed"},
        {"languages/c17.md", R"raw_embed(# C17 / C11 Coding Guidelines

## Core Principles
- **Standard Version**: Prefer ISO C17 / C11 standards (`-std=c17`).
- **Memory Safety**: Check return values of `malloc`, `calloc`, and `realloc`. Always `free` dynamically allocated memory.
- **Pointer Safety**: Check pointers for NULL before dereferencing. Avoid buffer overflows by using bounded string functions (`snprintf`, `strncpy`, `strncat`).
- **Header Guards**: Use `#ifndef FILENAME_H` / `#define FILENAME_H` or `#pragma once`.
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
        {"languages/rust2021.md", R"raw_embed(# Rust 2021 Edition Guidelines

## Core Principles
- **Edition**: Use Rust 2021 Edition.
- **Ownership & Borrowing**: Prefer explicit lifetime annotations, immutable borrows (`&T`), and RAII pattern (`Drop`).
- **Error Handling**: Use `Result<T, E>` and `Option<T>` with `?` operator instead of `panic!` or `unwrap()` in library code.
- **Safety**: Avoid `unsafe` blocks unless interfacing directly with foreign C APIs, and document all safety invariants.
)raw_embed"},
        {"languages/typescript.md", R"raw_embed(# TypeScript / JavaScript Guidelines

## Core Principles
- **Strict Typing**: Enable `strict: true` in `tsconfig.json`. Avoid `any`; use `unknown` or specific interface types.
- **Async Programming**: Prefer `async` / `await` over explicit Promise chains.
- **Modules**: Use ES module syntax (`import` / `export`).
)raw_embed"},
        {"languages/verilog.md", R"raw_embed(# Verilog / SystemVerilog Guidelines

## Core Principles
- **Modeling**: Separate combinational (`always_comb` / `always @(*)`) logic from sequential (`always_ff` / `always @(posedge clk)`) logic.
- **Assignments**: Use non-blocking assignments (`<=`) in sequential blocks and blocking assignments (`=`) in combinational blocks.
- **Synthesis**: Avoid un-synthesizable constructs (delays `#`, initial blocks for logic) in RTL modules.
)raw_embed"},
    };
    return docs;
}

} // namespace turbostar
