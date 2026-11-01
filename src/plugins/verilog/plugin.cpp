#include "agentlib/tool_registry.h"

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

/*
 * Plugin dynamic entry point invoked when loaded by plugin_loader.
 */
void plugin_run(void)
{
	register_vcd_file_info();
	register_vcd_wave();
	register_vcd_signal_labels();
	register_vcd_search();

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

2. **Define Value Mappings with Wildcards (`vcd_signal_labels`)**:
   Map opcode bytes or FSM state numbers across all instances using wildcard signal names (`*`) and binary bitmasks (`?`):
   `vcd_signal_labels(path="sim/dump.vcd", signal="*opcode", labels={"0x06": "LAI", "0x36": "LMA", "b01???01": "ALU_OP"})`
   To inspect all active label mappings for a file, call without labels:
   `vcd_signal_labels(path="sim/dump.vcd")`

3. **Locate Cycle Intervals of Interest (`vcd_search`)**:
   Find exact clock cycle ranges matching an opcode, state name, or bit pattern (supports hex, binary wildcards, or labels):
   `vcd_search(path="sim/dump.vcd", signal="opcode", value="LMA")`

4. **Sample Synchronous Waveforms at Clock Edges (`vcd_wave`)**:
   Query selected signals across the matched cycle window. Multi-bit buses default to hex; use `b:` prefix for binary bit-masks. Consecutive idle cycles (>= 3 ticks of no signal changes) are automatically collapsed:
   `vcd_wave(path="sim/dump.vcd", signals=["rst", "current_ip", "opcode", "b:flags", "A_val"], start_tick=4, end_tick=15)`
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

	agentlib::tool_registry::get_instance().unregister_tool_family("verilog");
}

}
