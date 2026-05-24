# Clang-Format Style Detection Architecture

## Overview
This document outlines the proposed architecture for an automatic `clang-format` style detector. The goal is to reverse-engineer a project's coding style from its existing source files and generate an accurate `.clang-format` file when one is missing.

This feature utilizes a **hybrid approach**, leveraging an LLM for rapid initial approximations and a local algorithmic loop for precise fine-tuning.

## The Hybrid Approach

### Phase 1: LLM "Rough Draft" (The Heuristic Phase)
Instead of starting a search algorithm completely blind (which leads to an intractable $2^N$ search space), we use the LLM to establish a baseline.

1. **Sampling:** Select a representative sample of `.cpp` and `.h` files from the project (e.g., 3-5 files, totaling ~1000 lines).
2. **LLM Prompting:** Provide the code sample to the LLM and ask it to identify the closest standard "Base Style" (LLVM, Google, Chromium, Mozilla, WebKit, Microsoft, or GNU) and guess the major structural variables:
    * `IndentWidth`
    * `TabWidth`
    * `UseTab`
    * `ColumnLimit`
    * `BreakBeforeBraces`
3. **Output:** The LLM returns a minimal, valid `.clang-format` starting point.

### Phase 2: Local Algorithmic Fine-Tuning (The Empirical Phase)
Using the LLM's draft as a starting point, a local loop tests specific formatting options to find the configuration that best matches the actual code.

1. **Option Space Definition:** Define a subset of highly-variable `clang-format` keys that often deviate from base styles (e.g., `PointerAlignment`, `SpaceBeforeAssignmentOperators`, `AlignTrailingComments`).
2. **Iterative Diffing:** For each option in the space:
    * Generate a temporary `.clang-format` file with the option toggled (e.g., `true` vs `false`).
    * Run `clang-format` on the sample files in-memory.
    * Use TurboStar's existing diffing library to calculate the "diff score" (number of changed lines/characters).
3. **Selection:** The state that produces the smallest diff score is locked in. Because the LLM provided a solid baseline, this optimization is essentially a linear sequence of independent binary choices (2 invocations per option), turning a $O(2^N)$ problem into an $O(N)$ problem.

## Implementation Details

### Required Components
* **LLM Tooling:** An internal prompt sequence designed specifically for format analysis.
* **Diff Engine:** TurboStar's existing diff logic must be accessible to the optimization loop to evaluate fitness.
* **Clang-Format Runner:** A function to invoke the local `clang-format` binary on memory buffers efficiently (avoiding disk I/O where possible).

### The "Fitness" Function
The score used to evaluate a configuration should penalize:
1. Lines changed.
2. Characters added or removed.

If an option yields no difference in the diff score (e.g., the option applies to a language feature not present in the sample), it defaults to the Base Style's setting.

## Advantages of the Hybrid Model
* **Speed:** The LLM bypasses the need to search the massive space of Base Styles and core indentation rules. The local loop only has to run a few dozen times instead of thousands.
* **Accuracy:** The local loop corrects any LLM hallucinations and captures subtle nuances the LLM might miss.
* **Feasibility:** Leveraging the existing diff library significantly reduces the complexity of the empirical phase.

## Future Considerations
* **Confidence Scoring:** The final output could present a "confidence score" based on how much of the original codebase still needed reformatting even with the optimal detected style.
* **Incremental Application:** Allow the user to apply the generated `.clang-format` interactively, reviewing the diffs before committing the file to the repository.
