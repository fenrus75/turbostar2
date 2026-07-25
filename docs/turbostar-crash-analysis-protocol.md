# Turbostar Crash Analysis Protocol

When analyzing a crash report (from a crash log, coredump, or test failure), AI agents **MUST** execute the 3-step **What – How – Where** analysis protocol defined below.

---

## Step 1: WHAT (Crash Identification)

Identify the precise failure condition and location from the crash report and source code.

* **Objective:** Determine the exact line, symbol, and invalid state that triggered the failure (e.g., `[editor_events.cpp:142] Pointer 'doc' was nullptr during dereference`).
* **Output:** Write a 1–2 sentence "What" summary. This summary **MUST** form the first line/paragraph of your user report and Git commit message.

---

## Step 2: HOW (Root-Cause Tracing)

Trace backwards through execution paths to determine *why* the invalid state occurred. Repeat the "How" query recursively (up to 3–4 levels) until reaching the true root cause.

Classify the analysis under one of three categories:

### A. Positive How (Explicit Assignment)
An explicit execution path assigned an invalid value (e.g., `nullptr`, invalid index, out-of-bounds size).
* **Action:** Trace the assignment chain step-by-step:
  - *Q1: How did `doc` become nullptr at line 142?*
    *A1: Assigned from `get_active_document()` at line 110.*
  - *Q2: How did `get_active_document()` return nullptr?*
    *A2: Returns nullptr when `window_count == 0` during workspace teardown.*
* **Report:** Include each Q&A round in your response and commit body.

### B. Negative How (Missing Initialization / Unhandled Path)
The state was never initialized or assigned a valid value.
* **Action:** Inspect normal instantiation patterns for the variable/field. Determine why normal initialization/assignment code path was bypassed (e.g., unexpected early return, unhandled event type, or race condition).

### C. Unknowable How (Insufficient Crash Data)
The crash report lacks sufficient context to prove the root cause.
* **Action:** Do **NOT** write a speculative fix. Instead, improve observability by adding structured logging (`event_logger`), assertions, or telemetry at key decision points so future crashes capture complete diagnostic state.

### D. Testcase Verification (Validating the "How")
At the conclusion of the **How** analysis (for Positive and Negative How), **MUST** attempt to create a minimal testcase that reproduces the root-cause state.
* **Validation Rule:** The testcase **MUST** fail prior to applying the fix (verifying your "How" hypothesis) and **MUST** pass once the fix is applied (proving the fix works).
* **Fallback:** If an automated testcase is not technically feasible (e.g., hardware/OS-specific signal scenarios), document the explicit manual reproduction sequence in your analysis.

---

## Step 3: WHERE (Fix Location)

Determine the correct location in the codebase for the fix based on Step 2.

1. **Root-Cause Fix (Preferred):** Place the fix at the origin site identified in Step 2 (e.g., correct state transition, validate function arguments, or fix initialization logic).
2. **Defensive Guard (Fallback):** Avoid placing a simple NULL check or dummy fallback at the crash site unless handling the condition locally is valid domain logic.
3. **Commit Message Format:** Ensure the final Git commit message includes:
   - Line 1: **WHAT** summary
   - Body: **HOW** trace (Q&A steps), testcase verification result, and **WHERE** fix rationale.
