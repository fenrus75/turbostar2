# Crash & Bug Analysis Protocol (What – How – Where)

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
