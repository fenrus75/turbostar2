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
- **Instructions**: Provide overall context and review criteria in `instructions`.
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
        {"languages/c17.md", R"raw_embed(# C17 Development Guidelines

## Code Conventions
- **Language Standard**: ISO C17 (`-std=c17`).
- **Include Guards**: Prefer `#pragma once` or standard `#ifndef` include guards for header files.
- **Const Correctness**: Use `const` for read-only pointer arguments (`const char *`, `const uint8_t *`).
- **Memory Management**: Check return values of `malloc`, `calloc`, and `realloc`. Ensure every allocated resource is freed and pointers cleared (`ptr = NULL`). Note that `free(NULL)` is valid, so avoid unnecessary `if (ptr) free(ptr)` wrappers.
- **Initialization**: Always initialize variables and struct instances (e.g. `{0}` or `memset`) prior to use.
- **File Organization**: Place each module/component in a dedicated `.c` file and matching `.h` header in the same directory.

## Security Considerations
- **Security-First Mindset**: Adopt a security-first mindset across all generated code.
- **Untrusted Input**: Clearly label variables and parameters holding untrusted (user/network) data in comments.
- **Early Validation**: Sanity-check and validate all untrusted input as early as possible.
- **Bounds Checking**: Verify all buffer and array accesses against underflows and overflows.
- **Buffer Size Parameters**: Always pass explicit buffer size parameters (`size_t buf_size`) alongside pointer arguments. Avoid unsafe string functions (`strcpy`, `sprintf`); use bounded alternatives (`snprintf`, `strncpy`).

## Code Commenting Conventions
- **Intent over Logic**: Focus comments on rationale (the "why") and design goals rather than restating code logic.
- **Ownership & Constraints**: Document pointer ownership, memory lifecycles, thread-safety expectations, and function constraints.
- **Mutex Declarations**: Precede every mutex declaration in header files with a comment block explaining:
  1. What specific data or resources the mutex protects.
  2. Locking rules, lifecycle, or ordering guidelines.
)raw_embed"},
        {"languages/cpp23.md", R"raw_embed(# C++23 Development Guidelines

## Code Conventions
- **Language Standard**: C++23. Use modern features (`std::format`, `std::string_view`, `std::span`, `constexpr`, `noexcept`, structured bindings).
- **Include Guards**: Prefer `#pragma once`.
- **Memory Management**: Follow RAII strictly; avoid raw `new` and `delete`, favoring `std::make_unique` and `std::make_shared`.
- **String & Sequence Handling**: Prefer `std::string_view` for read-only string function parameters over `const std::string &`, and `std::span<const T>` for read-only contiguous sequence parameters over `const std::vector<T> &` to avoid unnecessary heap allocations; use `std::string` / `std::vector` when ownership or mutation is required. Avoid raw `char *` except when interfacing with C APIs or the OS kernel.
- **Standard Library**: Prefer C++ Standard Library containers and algorithms over custom implementations.
- **File Organization**: Place each class in a dedicated `.cpp` file and matching header in the same directory. All `#include ""` directives should be relative to `src/`.
- **Constexpr & Qualifiers**: Label methods and parameters `constexpr`, `const`, `std::string_view`, `std::span`, and `noexcept` when appropriate.

## Security Considerations
- **Security-First Mindset**: Adopt a security-first mindset across all generated code.
- **Untrusted Input**: Clearly label variables and parameters holding untrusted (user/network) data in comments.
- **Early Validation**: Sanity-check and validate all untrusted input as early as possible.
- **Bounds Checking**: Verify all buffer and array accesses against underflows and overflows.

## Code Commenting Conventions
- **Intent over Logic**: Focus comments on rationale (the "why") and design goals rather than restating code logic.
- **Ownership & Constraints**: Document ownership, thread-safety expectations, and function constraints.
- **Mutex Declarations**: Precede every mutex declaration in header files with a comment block explaining:
  1. What specific data or resources the mutex protects.
  2. Locking rules, lifecycle, or ordering guidelines.
- **Subclass Header Documentation**: When creating a base class or adding a subclass, include or update a comment block table directly above the parent class definition detailing all derived subclasses and their file locations:
```cpp
/*

# subclasses of <parent class>

| subclass     | filename                                             |
| ------------ | ---------------------------------------------------- | 
| <subclass 1> | <project relative path to the header for subclass 1> |

*/
```

## Unit Test Guidelines
- **Test Isolation**: Every unit test MUST isolate the `HOME` environment variable to a temporary directory to prevent race conditions during parallel test execution. Call `test_watchdog::setup_watchdog()` at the start of `main()`.
)raw_embed"},
        {"languages/python311.md", R"raw_embed(# Python 3.11 Development Guidelines

## Code Conventions
- **Language Standard**: Python 3.11+.
- **Style Guide**: Follow PEP8 style guide and naming conventions.
- **Type Annotations**: Consistently add PEP 484 type annotations (`type_a | type_b`, `list[str]`, `dict[str, int]`) to all function arguments, return values, and variable declarations.
- **Virtual Environments**: Prefer running Python scripts within isolated environments or VFS `tmp://` scratch spaces.

## Security Considerations
- **Security Validation**: Code executed or created must pass `bandit` security checks (no `shell=True` in subprocess calls, no unvalidated `eval()` or `exec()`).
- **Input Sanitization**: Validate all external paths, user input strings, and API payloads before processing.
)raw_embed"},
        {"languages/rust2021.md", R"raw_embed(# Rust 2021 Development Guidelines

## Code Conventions
- **Language Edition**: Rust 2021 Edition.
- **Ownership & Borrowing**: Follow Rust ownership, borrowing, and lifetime rules strictly. Prefer immutable borrows (`&T`) and RAII (`Drop`).
- **Error Handling**: Use `Result<T, E>` and `Option<T>` with `?` operator propagation. Avoid `panic!` or `unwrap()` in library code.
- **Safety**: Avoid `unsafe` blocks unless interfacing directly with C FFI or OS primitives. Document all safety invariants with `# Safety` comments.
- **Documentation**: Document all public types, traits, and functions with doc comments (`///`).
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
