# Bugfixing Protocol (Assess – Reproduce – Verify – Fix – Confirm)

When fixing a reported bug (wrong output, missed feature, edge-case logic, or test
failure), you **MUST** execute the following protocol in order. It guarantees the fix
is correct, covered, and confined to the true root cause.

---

## Step 1: ASSESS (Gauge test coverage in the suspected area)

Before writing or changing any source, determine how well the affected area is tested
so you know how much guard-railing the fix will need.

- **Locate the relevant test file(s)** and check the coverage around the suspected
  surface (matching format/flag/type combinations, boundaries, edge cases).
- **If testing is weak or absent, add more (or many) baseline tests first** to pin the
  current behavior — under-tested areas are where regressions ship — before attempting
  to reproduce or fix. Note that this may uncover other bugs in the underlying code.
- **Record what is already covered** so you add tests for the gaps rather than
  duplicating existing ones.

---

## Step 2: REPRODUCE (Get a failing case BEFORE touching source)

Do not edit source until you can observe the bug.

- **Build a minimal failing reproducer** from the report (minimal inputs, minimal
  format/arguments/call site).
- **Prefer the project's real test harness** (`fs_run_tests` on the existing suite)
  over throwaway probes. The correct reproducer *is* a new test in the trailing
  baseline suite from Step 1.
- **Confirm the failure** is observed (test FAILS, or output differs from expected)
  before proceeding. An unobserved bug is not yet a bug you understand.
- **Admit defeat** when the issue is so complicated that no reasonable test can be
  created — raise this early rather than burning time on speculative fixes.

---

## Step 3: VERIFY (Pin the expected behavior with a failing test)

With baseline coverage in place (Step 1), add the test that pins the *correct*,
post-fix behavior **before** implementing the fix.

- **Add a test for the reported case** that asserts the correct (post-fix) behavior.
- **Cover the fix's neighbors**, not just the exact report — flag combinations,
  boundaries, wide/narrow variants.
- **Run the new test and confirm it FAILS** against the current code. A test that
  passes before the fix is testing nothing.

---

## Step 4: FIX (Root cause, not symptom)

Locate and fix the true root cause.

- **Trace to the origin** of the incorrect behavior, not the first place you notice it
  (see `crash_analysis.md` for the What/How/Where technique applied to failure states).
- **Root-Cause Fix (Preferred)**: change the origin site so the behavior is correct.
- **No Superficial Symptom Patches**: avoid masking, special-casing, or
  swallow-guarding at the symptoms unless that is valid domain logic.
- **Add comments**: Presume something is unusual in this area; add comments that
  describe the correct high level intent and boundary conditions.
---

## Step 5: CONFIRM (Fail-then-Pass)

- **Re-run the full relevant suite**: the new tests must now PASS, and existing
  tests must not REGRESS (run neighboring test files too).
- **Sanity-check neighbors**: confirm the fix did not alter unrelated behaviors
  (e.g. default and adjacent flag paths).
- Keep the change **focused**: code + tests for this bug only. Leave chore/tracking
  files, unrelated formatting, and changelog curation out unless instructed otherwise.

---

*Companion protocol: `crash_analysis.md` for crash/undefined-state analysis. Share the
test-verification (fail-then-pass) and root-cause-over-symptom principles with it to
avoid drift.*