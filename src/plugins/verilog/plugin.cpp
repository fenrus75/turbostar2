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

/*
 * Plugin dynamic entry point invoked when loaded by plugin_loader.
 */
void plugin_run(void)
{
	register_vcd_file_info();
	register_vcd_wave();

	agentlib::tool_registry::get_instance().register_tool_family(
		"verilog",
		"Activate when working with Verilog/SystemVerilog chip and FPGA designs",
		R"(### Verilog & Hardware Design Tool Family (`verilog`)

Tools in this family assist in digital hardware design, FPGA engineering, simulation verification, and waveform analysis.

#### Recommended Workflow: Inspecting a Simulation Dump (.vcd)

To debug a digital design without blowing your context window, follow this two-step pattern:

1. **Discover Simulation Context & Signal Hierarchy (`vcd_file_info`)**:
   Inspect timescale, duration, detected reference clock, and discover signals in the module of interest:
   `vcd_file_info(path="sim/dump.vcd", scope="tb_main.uut.cpu8008", max_signals=30)`

2. **Sample Synchronous Waveforms at Clock Edges (`vcd_wave`)**:
   Query selected signals across a specific clock tick window. Multi-bit buses default to hex; use `b:` prefix for binary bit-masks. Short signal names or suffixes (e.g. `"opcode"`, `"flags"`) are automatically resolved, matching the prefix of preceding signals in the list:
   `vcd_wave(path="sim/dump.vcd", signals=["rst", "current_ip", "opcode", "b:flags", "A_val"], start_tick=0, end_tick=15)`

3. **Inspect Half-Cycles or Asynchronous Transitions (Optional)**:
   To inspect half-cycle phases or asynchronous transitions around clock edges, sample on both edges:
   `vcd_wave(path="sim/dump.vcd", signals=["clk", "mem_write_en", "mem_write_addr"], edge="both", start_tick=10, end_tick=15)`
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

	agentlib::tool_registry::get_instance().unregister_tool_family("verilog");
}

}
