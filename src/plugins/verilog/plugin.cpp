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

	bool has_yosys = !fs_utils::find_executable("yosys").empty();
	if (has_yosys) {
		register_vcd_signal_to_code();
	}

	agentlib::tool_registry::get_instance().register_tool_family(
		"verilog",
		"Activate when working with Verilog/SystemVerilog chip and FPGA designs",
		R"(### Verilog & Hardware Design Tool Family (`verilog`)

Tools in this family assist in digital hardware design, FPGA engineering, simulation verification, and waveform analysis.

#### Recommended Workflow: Inspecting a Simulation Dump (.vcd)

To debug a digital design without blowing your context window, follow this workflow:

1. **Discover Simulation Context & Signal Hierarchy (`vcd_file_info`)**:
   Inspect timescale, duration, detected reference clock, and discover signals in the module of interest:
   `vcd_file_info(path="sim/dump.vcd", scope="tb_main.uut.cpu8008", max_signals=30)`

2. **Define Value Mappings & Qualifiers (`vcd_signal_labels`, `vcd_signal_qualifier`)**:
   Map opcode bytes or state numbers across instances using wildcard signal names (`*`) and binary bitmasks (`?`):
   `vcd_signal_labels(path="sim/dump.vcd", signal="*opcode", labels={"0x06": "LAI", "0x36": "LMA", "b01???01": "ALU_OP"})`
   Bind data buses to valid/enable strobes (or active-low '~cs_n') so invalid cycles mask as '-' and collapse cleanly:
   `vcd_signal_qualifier(path="sim/dump.vcd", signal="opcode_data", qualifier="opcode_data_valid")`

3. **Locate Cycle Intervals of Interest (`vcd_search`)**:
   Find exact clock cycle ranges matching an opcode, state name, or bit pattern (supports multi-condition AND matching):
   `vcd_search(path="sim/dump.vcd", conditions={"opcode_data": "LMA", "opcode_data_valid": "1"})`

4. **Sample Synchronous Waveforms at Clock Edges (`vcd_wave`)**:
   Query selected signals across the matched cycle window. Multi-bit buses default to hex; use `b:` prefix for binary bit-masks. Consecutive idle cycles (>= 3 ticks of no signal changes) are automatically collapsed:
   `vcd_wave(path="sim/dump.vcd", signals=["rst", "current_ip", "opcode_data", "b:flags"], start_tick=4, end_tick=15)`

5. **Compare Against Reference / Golden Run (`vcd_compare`)**:
   Pinpoint the exact clock cycle of divergence between golden and candidate traces, listing differing signals while ignoring invalid bus cycles:
   `vcd_compare(path_a="sim/golden.vcd", path_b="sim/candidate.vcd", scope="tb_main.uut")`

6. **Trace Signal to Source RTL (`vcd_signal_to_code`)**:
   Map a simulation signal back to its source file declarations across the hierarchy, its driving cell/port, and downstream consumers:
   `vcd_signal_to_code(signal="tb_main.uut.cpu8008.A_val")`

7. **Analyze Logic Depth & Timing Feasibility (`verilog_logic_depth`)**:
   Evaluate pre-place-and-route timing boundaries (In->Reg, Reg->Out, Reg->Reg, In->Out) and top critical paths:
   `verilog_logic_depth(module="alu", top_k=3)`

8. **Batch Update Timing Sidecars (`verilog_update_timing`)**:
   Scan project modules and update all `<module>.timing.md` interface contract sidecars in batch:
   `verilog_update_timing(path="sv", target="Generic LUT4")`

9. **Configure Project FPGA Settings (`verilog_project_settings`)**:
   Query or configure target hardware architecture, device, package, clock frequency, and pin constraints:
   `verilog_project_settings(target="ice40", device="hx8k", package="cb132", top_module="main", freq_mhz=60.0)`

10. **Analyze Physical Place-and-Route Timing (`verilog_pnr_timing`)**:
   Pinpoint post-route physical timing bottlenecks and Fmax, decomposed into logic vs wire routing delay:
   `verilog_pnr_timing(top_k=3)`
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

	agentlib::tool_registry::get_instance().unregister_tool_family("verilog");
}

}
