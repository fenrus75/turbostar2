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
void register_vcd_signal_qualifier(void);
void unregister_vcd_signal_qualifier(void);

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
   Find exact clock cycle ranges matching an opcode, state name, or bit pattern (automatically ignores invalid cycles):
   `vcd_search(path="sim/dump.vcd", signal="opcode_data", value="LMA")`

4. **Sample Synchronous Waveforms at Clock Edges (`vcd_wave`)**:
   Query selected signals across the matched cycle window. Multi-bit buses default to hex; use `b:` prefix for binary bit-masks. Consecutive idle cycles (>= 3 ticks of no signal changes) are automatically collapsed:
   `vcd_wave(path="sim/dump.vcd", signals=["rst", "current_ip", "opcode_data", "b:flags"], start_tick=4, end_tick=15)`
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

	agentlib::tool_registry::get_instance().unregister_tool_family("verilog");
}

}
