#include "agentlib/tool_registry.h"
#include "fs_utils.h"

extern "C" {

const char *plugin_name(void)
{
	return "Verilog/SystemVerilog Tools";
}

const char *plugin_description(void)
{
	return "Provides tools for chip and FPGA design, including waveform and simulation inspection (VCD).";
}

void register_vcd_file_info(void);
void unregister_vcd_file_info(void);
void register_vcd_wave(void);
void unregister_vcd_wave(void);
void register_vcd_signal_labels(void);
void unregister_vcd_signal_labels(void);
void register_vcd_search(void);
void unregister_vcd_search(void);
void register_vcd_signal_qualifier(void);
void unregister_vcd_signal_qualifier(void);
void register_vcd_signal_to_code(void);
void unregister_vcd_signal_to_code(void);
void register_vcd_compare(void);
void unregister_vcd_compare(void);
void register_verilog_logic_depth(void);
void unregister_verilog_logic_depth(void);
void register_verilog_update_timing(void);
void unregister_verilog_update_timing(void);
void register_verilog_project_settings(void);
void unregister_verilog_project_settings(void);
void register_verilog_pnr_timing(void);
void unregister_verilog_pnr_timing(void);
void register_verilog_simulate(void);
void unregister_verilog_simulate(void);
void register_verilog_configure_testbench(void);
void unregister_verilog_configure_testbench(void);
void register_verilog_compile(void);
void unregister_verilog_compile(void);

/*
 * Plugin dynamic entry point invoked when loaded by plugin_loader.
 */
void plugin_run(void)
{
	register_vcd_file_info();
	register_vcd_wave();
	register_vcd_signal_labels();
	register_vcd_search();
	register_vcd_signal_qualifier();
	register_vcd_compare();
	register_verilog_logic_depth();
	register_verilog_update_timing();
	register_verilog_project_settings();
	register_verilog_pnr_timing();
	register_verilog_simulate();
	register_verilog_configure_testbench();
	register_verilog_compile();

	bool has_yosys = !fs_utils::find_executable("yosys").empty();
	if (has_yosys) {
		register_vcd_signal_to_code();
	}

	agentlib::tool_registry::get_instance().register_tool_family(
		"verilog",
		"Activate when working with Verilog/SystemVerilog chip and FPGA designs",
		R"(### Verilog & Hardware Design Tool Family (`verilog`)

Tools in this family assist in SystemVerilog/Verilog RTL development, FPGA synthesis & place-and-route, simulation verification, timing closure, and waveform analysis.

#### 1. Standard Edit – Compile – Test – Commit Workflow
- **Initial Setup (`verilog_project_settings`, `verilog_configure_testbench`)**:
  Configure target FPGA hardware (`target`, `device`, `package`, `top_module`, `freq_mhz`, `sv_path="src"`) and associate testbenches with RTL modules (`modules`, `level="quick"|"comprehensive"|"extreme"`, and optional per-module `hierarchical=true` default) in `turbostar.json`.
- **Compile & Health Check (`verilog_compile`)**:
  Run `verilog_compile(module="...", lint_only=true)` for a fast Verilator syntax/lint pass, or `verilog_compile(module="...")` for full Verilator + Yosys synthesis + `nextpnr` place-and-route. It scopes files to the module's hierarchy, separates correctness/synthesis warnings from style notes, flags inferred latches and combinational loops, and writes the full raw log to `tmp://synthesis.log`.
- **Simulate (`verilog_simulate`)**:
  Run associated testbenches in parallel via Verilator (`verilog_simulate(module="...")`). Give each testbench a unique `$dumpfile("wave/dump_<tb>.vcd")` so parallel runs do not collide, and emit explicit `PASS` or `FAIL`/`$error` assertions so benches are graded `PASS` rather than `INFO (no assertions)`.

#### 2. Waveform Debugging Workflow (.vcd)
To debug simulation waveforms without blowing out your context window:
1. **Discover Hierarchy & Clocks (`vcd_file_info`)**:
   `vcd_file_info(path="wave/dump_main.vcd", scope="tb_main.uut", max_signals=30)`
2. **Define Value Labels & Valid Qualifiers (`vcd_signal_labels`, `vcd_signal_qualifier`)**:
   Note: `typedef enum` FSM states in RTL are auto-discovered. For opcodes or raw buses, register project-wide labels (`vcd_labels.json`) and valid/enable strobes so invalid cycles mask as `-`:
   `vcd_signal_labels(path="wave/dump_main.vcd", signal="*opcode", labels={"0x06": "LAI", "b01???01": "ALU_OP"})`
   `vcd_signal_qualifier(path="wave/dump_main.vcd", signal="opcode_data", qualifier="opcode_data_valid")`
3. **Locate Cycles of Interest (`vcd_search`)**:
   `vcd_search(path="wave/dump_main.vcd", conditions={"opcode_data": "LAI", "opcode_data_valid": "1"})`
4. **Sample Synchronous Waveforms (`vcd_wave`)**:
   `vcd_wave(path="wave/dump_main.vcd", signals=["rst", "current_ip", "opcode_data", "b:flags"], start_tick=4, end_tick=15)`
5. **Compare Against Golden / Previous Run (`vcd_compare`)**:
   `verilog_simulate` automatically snapshots `<stem>.previous.vcd` and `<stem>.lastpass.vcd` and reports divergence. Use `vcd_compare` for detailed cycle-accurate diffing:
   `vcd_compare(path_a="wave/dump_main.lastpass.vcd", path_b="wave/dump_main.vcd", scope="tb_main.uut")`
6. **Trace Signal to RTL (`vcd_signal_to_code`)**:
   `vcd_signal_to_code(signal="tb_main.uut.cpu8008.A_val")`

#### 3. Dual-View Timing Optimization Strategy
Timing closure works best when combining the structural view with the post-routing physical view:
- **Structural Logic Depth (`verilog_logic_depth`, `verilog_update_timing`)**:
  Measures LUT levels between flip-flops (`In->Reg`, `Reg->Reg`, `Reg->Out`, `In->Out`) to reveal the underlying architectural cause of long combinational chains (`verilog_logic_depth(module="alu", top_k=3)`).
- **Physical Place-and-Route Timing (`verilog_pnr_timing`)**:
  Runs multi-seed `nextpnr` to report post-routing $F_{\max}$, critical paths decomposed into logic vs. routing wire delay, and net fan-out (`verilog_pnr_timing(top_k=3)`).
)"
	);
}

/*
 * Plugin dynamic unload callback invoked on shutdown or library unload.
 */
void plugin_unload(void)
{
	unregister_vcd_file_info();
	unregister_vcd_wave();
	unregister_vcd_signal_labels();
	unregister_vcd_search();
	unregister_vcd_signal_qualifier();
	unregister_vcd_signal_to_code();
	unregister_vcd_compare();
	unregister_verilog_logic_depth();
	unregister_verilog_update_timing();
	unregister_verilog_project_settings();
	unregister_verilog_pnr_timing();
	unregister_verilog_simulate();
	unregister_verilog_configure_testbench();
	unregister_verilog_compile();

	agentlib::tool_registry::get_instance().unregister_tool_family("verilog");
}

}
