// Tested source file: src/plugins/verilog/netlist_graph_analyzer.cpp
#include "plugins/verilog/netlist_graph_analyzer.h"
#include "fs_utils.h"
#include "unit/test_watchdog.h"

#include <cassert>
#include <filesystem>
#include <iostream>

void test_synthetic_register_module()
{
	// Synthetic JSON netlist representing a simple register with:
	// - clk (Clock)
	// - rst (Register reset)
	// - load (1 layer mux -> DFF D)
	// - load_value[1:0] (1 layer mux -> DFF D)
	// - value[1:0] (0 layers from DFF Q -> Output)
	// - bypass_in (2 layers -> bypass_out)
	// - bypass_out (2 layers from bypass_in)
	std::string json_data = R"({
  "creator": "Yosys test",
  "modules": {
    "test_reg": {
      "attributes": {
        "top": "00000000000000000000000000000001"
      },
      "ports": {
        "clk": {
          "direction": "input",
          "bits": [ 2 ]
        },
        "rst": {
          "direction": "input",
          "bits": [ 3 ]
        },
        "load": {
          "direction": "input",
          "bits": [ 4 ]
        },
        "load_value": {
          "direction": "input",
          "bits": [ 5, 6 ]
        },
        "value": {
          "direction": "output",
          "bits": [ 7, 8 ]
        },
        "bypass_in": {
          "direction": "input",
          "bits": [ 9 ]
        },
        "bypass_out": {
          "direction": "output",
          "bits": [ 12 ]
        },
        "dangling_out": {
          "direction": "output",
          "bits": [ 99 ]
        },
        "const_out": {
          "direction": "output",
          "bits": [ "0" ]
        }
      },
      "cells": {
        "mux_load_0": {
          "type": "$mux",
          "attributes": { "src": "test.sv:15" },
          "port_directions": { "A": "input", "B": "input", "S": "input", "Y": "output" },
          "connections": { "A": [ 7 ], "B": [ 5 ], "S": [ 4 ], "Y": [ 10 ] }
        },
        "dff_val_0": {
          "type": "$adff",
          "attributes": { "src": "test.sv:20" },
          "port_directions": { "CLK": "input", "ARST": "input", "D": "input", "Q": "output" },
          "connections": { "CLK": [ 2 ], "ARST": [ 3 ], "D": [ 10 ], "Q": [ 7 ] }
        },
        "lut_bypass_1": {
          "type": "LUT4",
          "attributes": { "src": "test.sv:30" },
          "port_directions": { "I0": "input", "O": "output" },
          "connections": { "I0": [ 9 ], "O": [ 11 ] }
        },
        "lut_bypass_2": {
          "type": "LUT4",
          "attributes": { "src": "test.sv:32" },
          "port_directions": { "I0": "input", "O": "output" },
          "connections": { "I0": [ 11 ], "O": [ 12 ] }
        }
      },
      "netnames": {
        "clk": { "bits": [ 2 ] },
        "rst": { "bits": [ 3 ] },
        "load": { "bits": [ 4 ] },
        "load_value": { "bits": [ 5, 6 ] },
        "value": { "bits": [ 7, 8 ] },
        "bypass_in": { "bits": [ 9 ] },
        "bypass_out": { "bits": [ 12 ] }
      }
    }
  }
})";

	verilog::netlist_graph_analyzer analyzer;
	std::string err;
	bool ok = analyzer.load_from_json_string(json_data, err);
	assert(ok);
	assert(analyzer.get_module_name() == "test_reg");

	// Verify input ports
	const auto &inps = analyzer.get_input_ports();
	assert(inps.size() == 5);

	// clk -> Clock
	auto it_clk = std::find_if(inps.begin(), inps.end(), [](const auto &p) { return p.name == "clk"; });
	assert(it_clk != inps.end());
	assert(it_clk->endpoint == verilog::endpoint_kind::clock);
	assert(it_clk->endpoint_desc == "Clock");
	assert(it_clk->layers == -1);

	// rst -> Register (depth 0, direct to ARST)
	auto it_rst = std::find_if(inps.begin(), inps.end(), [](const auto &p) { return p.name == "rst"; });
	assert(it_rst != inps.end());
	assert(it_rst->endpoint == verilog::endpoint_kind::register_cell);
	assert(it_rst->layers == 0);

	// load -> Register (depth 1, passes through mux)
	auto it_load = std::find_if(inps.begin(), inps.end(), [](const auto &p) { return p.name == "load"; });
	assert(it_load != inps.end());
	assert(it_load->endpoint == verilog::endpoint_kind::register_cell);
	assert(it_load->layers == 1);

	// load_value -> Register (depth 1, passes through mux)
	auto it_lv = std::find_if(inps.begin(), inps.end(), [](const auto &p) { return p.name == "load_value"; });
	assert(it_lv != inps.end());
	assert(it_lv->bit_width == 2);
	assert(it_lv->endpoint == verilog::endpoint_kind::register_cell);
	assert(it_lv->layers == 1);

	// bypass_in -> Output (depth 2: lut_bypass_1 -> lut_bypass_2 -> bypass_out)
	auto it_bpi = std::find_if(inps.begin(), inps.end(), [](const auto &p) { return p.name == "bypass_in"; });
	assert(it_bpi != inps.end());
	assert(it_bpi->endpoint == verilog::endpoint_kind::output_port);
	assert(it_bpi->endpoint_desc == "Output");
	assert(it_bpi->layers == 2);

	// Verify output ports
	const auto &outps = analyzer.get_output_ports();
	assert(outps.size() == 4);

	// value -> Register (depth 0, direct from Q)
	auto it_val = std::find_if(outps.begin(), outps.end(), [](const auto &p) { return p.name == "value"; });
	assert(it_val != outps.end());
	assert(it_val->endpoint == verilog::endpoint_kind::register_cell);
	assert(it_val->endpoint_desc == "Register");
	assert(it_val->layers == 0);

	// bypass_out -> Input (depth 2 from bypass_in)
	auto it_bpo = std::find_if(outps.begin(), outps.end(), [](const auto &p) { return p.name == "bypass_out"; });
	assert(it_bpo != outps.end());
	assert(it_bpo->endpoint == verilog::endpoint_kind::input_port);
	assert(it_bpo->endpoint_desc == "Input");
	assert(it_bpo->layers == 2);

	// dangling_out -> Floating (no driver)
	auto it_dangling = std::find_if(outps.begin(), outps.end(), [](const auto &p) { return p.name == "dangling_out"; });
	assert(it_dangling != outps.end());
	assert(it_dangling->endpoint == verilog::endpoint_kind::floating);
	assert(it_dangling->endpoint_desc == "Floating");
	assert(it_dangling->layers == -1);

	// const_out -> Constant
	auto it_const = std::find_if(outps.begin(), outps.end(), [](const auto &p) { return p.name == "const_out"; });
	assert(it_const != outps.end());
	assert(it_const->endpoint == verilog::endpoint_kind::constant);
	assert(it_const->endpoint_desc == "Constant");
	assert(it_const->layers == 0);

	// Internal Reg -> Reg depth (dff_val_0 Q -> mux_load_0 -> dff_val_0 D = 1 layer)
	assert(analyzer.get_max_reg_to_reg_depth() == 1);

	// Test worst paths extraction
	auto worst = analyzer.get_worst_paths(std::nullopt, 5);
	assert(!worst.empty());
	assert(worst[0].depth == 2); // bypass path has depth 2
	assert(worst[0].hops.size() == 2);

	auto reg_worst = analyzer.get_worst_paths(verilog::path_category::reg_to_reg, 3);
	assert(!reg_worst.empty());
	assert(reg_worst[0].depth == 1);
	assert(reg_worst[0].startpoint == "value[0]");
	assert(reg_worst[0].endpoint == "value[0]");

	// Test markdown sidecar formatting
	std::string md = analyzer.generate_timing_markdown("Generic LUT4");
	assert(md.find("# Timing: `test_reg`") != std::string::npos);
	assert(md.find("Target: Generic LUT4") != std::string::npos);
	assert(md.find("Max Internal Reg -> Reg Depth: 1") != std::string::npos);
	assert(md.find("| `clk` | 1 | - | Clock |") != std::string::npos);
	assert(md.find("| `load` | 1 | 1 | Register |") != std::string::npos);
	assert(md.find("| `bypass_in` | 1 | 2 | Output |") != std::string::npos);
	assert(md.find("| `value` | 2 | 0 | Register |") != std::string::npos);
	assert(md.find("| `bypass_out` | 1 | 2 | Input |") != std::string::npos);
	assert(md.find("| `dangling_out` | 1 | - | Floating |") != std::string::npos);
	assert(md.find("| `const_out` | 1 | 0 | Constant |") != std::string::npos);
}

void test_real_netlist_files()
{
	verilog::netlist_graph_analyzer analyzer;
	std::string err;

	// Test with examples/test8008/build/netlist.json if it exists
	if (std::filesystem::exists("examples/test8008/build/netlist.json")) {
		bool ok = analyzer.load_from_file("examples/test8008/build/netlist.json", err);
		assert(ok);
		assert(analyzer.get_module_name() == "main");
		assert(!analyzer.get_input_ports().empty());
		assert(!analyzer.get_output_ports().empty());

		// In main: clk is clock input
		const auto &inps = analyzer.get_input_ports();
		auto it_clk = std::find_if(inps.begin(), inps.end(), [](const auto &p) { return p.name == "clk"; });
		if (it_clk != inps.end()) {
			assert(it_clk->endpoint == verilog::endpoint_kind::clock);
		}

		// Check worst paths and bottlenecks
		auto worst = analyzer.get_worst_paths(verilog::path_category::reg_to_reg, 3);
		assert(!worst.empty());
		assert(analyzer.get_max_reg_to_reg_depth() > 0);

		auto bottlenecks = analyzer.get_worst_bottlenecks(std::nullopt, 3);
		assert(!bottlenecks.empty());
		assert(!bottlenecks[0].endpoint.empty());
		assert(bottlenecks[0].depth > 0);

		std::string md = analyzer.generate_timing_markdown("Generic LUT4");
		assert(md.find("# Timing: `main`") != std::string::npos);
	}
}

void test_compact_signal_formatting()
{
	using verilog::netlist_graph_analyzer;
	std::vector<std::string> sigs = {"input1[0]", "input1[1]", "input1[2]", "input1[4]", "input1[5]", "carry_in"};
	std::string compacted = netlist_graph_analyzer::compact_signal_names(sigs);
	assert(compacted.find("`input1[2:0]`") != std::string::npos);
	assert(compacted.find("`input1[5:4]`") != std::string::npos);
	assert(compacted.find("`carry_in`") != std::string::npos);

	std::vector<verilog::source_signal_info> srcs = {
		{"input1[0]", "alu.sv:4", verilog::path_category::in_to_reg},
		{"input1[1]", "alu.sv:4", verilog::path_category::in_to_reg},
		{"input1[2]", "alu.sv:4", verilog::path_category::in_to_reg},
		{"carry_in", "alu.sv:19-24", verilog::path_category::reg_to_reg}
	};
	std::string formatted = netlist_graph_analyzer::format_source_ranges(srcs);
	assert(formatted.find("`input1[2:0]` (`alu.sv:4`)") != std::string::npos);
	assert(formatted.find("`carry_in` (`alu.sv:19-24`)") != std::string::npos);
}

int main()
{
	test_watchdog::setup_watchdog();
	std::cout << "Testing netlist_graph_analyzer..." << std::endl;

	test_synthetic_register_module();
	test_real_netlist_files();
	test_compact_signal_formatting();

	std::cout << "All netlist_graph_analyzer tests passed successfully!" << std::endl;
	return 0;
}
