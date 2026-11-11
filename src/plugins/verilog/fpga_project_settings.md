# FPGA Project Settings (`fpga_project_settings`)

## Goals & Constraints

- **Purpose**: Manages FPGA hardware project settings (`target`, `device`, `package`, `top_module`, `freq_mhz`, `pcf_file`, `sv_path`, `yosys_synth_args`), enabling seamless automated synthesis and place-and-route timing analysis without user micromanagement.
- **File Precedence**: Checks for `turbostar.json` first, then falls back to vendor-neutral `project.json` in the project root. When writing, updates existing files or creates `turbostar.json` formatted with 2-space indentation.
- **Smart Discovery**: Automatically discovers installed nextpnr and Yosys binaries on PATH, auto-detects synthesizable RTL directories (`sv`, `rtl`, `src`), and scans the workspace directory tree for pin constraint files (`*.pcf`). If exactly one `.pcf` file exists, it can be automatically suggested or utilized.
- **Contextual Guidance**: If mandatory settings are missing, generates structured markdown guidance paragraphs explaining what is missing, how the LLM can resolve it from Makefiles or board specs, and provides the exact remediation command.
- **Testbench Configuration (`testbenches`)**: Loads and saves many-to-many testbench-to-RTL-module associations (`testbench_entry`: `testbench`, `modules`, `level`) under the `"testbenches"` key in `turbostar.json` (or `project.json`) while preserving existing `"fpga"` and other top-level configuration objects.

## Lessons Learned

- **Indented JSON over flat INI**: Formatted JSON (`dump(2)`) avoids parsing ambiguities, escaping bugs, and allows rich data while remaining human-readable in git diffs.
- **Deterministic Scanning vs. Regexing Makefiles**: Makefiles vary wildly in structure; scanning for `.pcf` files and installed nextpnr binaries is 100% deterministic, whereas the LLM itself is best suited for interpreting complex Makefiles.
