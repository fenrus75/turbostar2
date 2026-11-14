// Tested source file: src/plugins/verilog/vcd_parser.cpp
#include "test_watchdog.h"
#include "plugins/verilog/vcd_parser.h"
#include "plugins/verilog/vcd_parser_cache.h"
#include "plugins/verilog/vcd_label_manager.h"
#include "plugins/verilog/vcd_file_info_tool.h"
#include "plugins/verilog/vcd_file_info_validator.h"
#include "plugins/verilog/vcd_wave_tool.h"
#include "plugins/verilog/vcd_wave_validator.h"
#include "plugins/verilog/vcd_signal_labels_tool.h"
#include "plugins/verilog/vcd_signal_labels_validator.h"
#include "plugins/verilog/vcd_search_tool.h"
#include "plugins/verilog/vcd_search_validator.h"
#include "plugins/verilog/vcd_signal_qualifier_tool.h"
#include "plugins/verilog/vcd_signal_qualifier_validator.h"
#include "plugins/verilog/yosys_netlist_reader.h"
#include "plugins/verilog/vcd_signal_to_code_tool.h"
#include "plugins/verilog/vcd_signal_to_code_validator.h"
#include "plugins/verilog/vcd_compare_tool.h"
#include "plugins/verilog/vcd_compare_validator.h"
#include "plugins/verilog/verilog_simulate_tool.h"
#include "plugins/verilog/verilog_simulate_validator.h"
#include "plugins/verilog/verilog_configure_testbench_tool.h"
#include "plugins/verilog/verilog_configure_testbench_validator.h"
#include "plugins/verilog/verilog_module_scanner.h"
#include "plugins/verilog/fpga_project_settings.h"
#include "agentlib/tool_context.h"
#include "fs_utils.h"
#include <cassert>
#include <iostream>
#include <fstream>
extern "C" {
void register_vcd_file_info();
void unregister_vcd_file_info();
void register_vcd_wave();
void unregister_vcd_wave();
void register_vcd_signal_labels();
void unregister_vcd_signal_labels();
void register_vcd_search();
void unregister_vcd_search();
void register_vcd_signal_qualifier();
void unregister_vcd_signal_qualifier();
void register_vcd_signal_to_code();
void unregister_vcd_signal_to_code();
void register_vcd_compare();
void unregister_vcd_compare();
void register_verilog_simulate();
void unregister_verilog_simulate();
void register_verilog_configure_testbench();
void unregister_verilog_configure_testbench();
}
int main()
{
	test_watchdog::setup_watchdog(90);
	const std::string vcd_path = "tests/fixtures/test_dump.vcd";
	std::filesystem::remove(vcd_path + ".labels.json");
	std::filesystem::remove("tests/fixtures/test_dump_org.vcd.labels.json");
	std::filesystem::remove("tests/fixtures/test_dump_org.vcd.qualifiers.json");
	std::filesystem::remove("tests/fixtures/test_dump_mutated.vcd.labels.json");
	std::filesystem::remove("tests/fixtures/test_dump_mutated.vcd.qualifiers.json");
	std::filesystem::remove("tests/fixtures/vcd_labels.json");
	std::filesystem::remove("vcd_labels.json");

	agentlib::tool_context global_ctx;
	global_ctx.fs_security.add_allowed_root("tests/fixtures", agentlib::access_type::read);
	global_ctx.fs_security.add_allowed_root("tests/fixtures", agentlib::access_type::write);
	global_ctx.fs_security.set_working_directory(".");
	{
		verilog::vcd_parser parser;
		std::string err;
		bool ok = parser.parse_all(vcd_path, err);
		assert(ok && err.empty());

		assert(parser.get_timescale() == "1ps");
		assert(parser.get_start_time() == 0);
		assert(parser.get_end_time() == 2020000);
		assert(parser.get_signals().size() == 234);

		// Clock detection
		auto clk = parser.detect_clock();
		assert(clk.has_value());
		assert(clk->signal_name == "tb_main.clk");
		assert(clk->period == 10000); // 10000ps
		assert(clk->posedge_count == 202);
		assert(clk->negedge_count == 202);

		// Signal lookup
		const auto *sig = parser.find_signal("tb_main.uut.cpu8008.opcode");
		assert(sig != nullptr);
		assert(sig->bit_width == 8);
		assert(sig->short_name == "opcode");

		// Filter signals
		auto cpu_signals = parser.filter_signals("tb_main.uut.cpu8008", "");
		assert(!cpu_signals.empty());

		auto addr_signals = parser.filter_signals("", "*addr*");
		assert(!addr_signals.empty());

		// Sampling posedges
		std::vector<std::string> requested = {
			"tb_main.uut.cpu8008.opcode",
			"b:tb_main.uut.cpu8008.flags",
			"tb_main.uut.cpu8008.current_ip"
		};
		std::string sample_err;
		auto samples = parser.sample_wave(
			"tb_main.clk",
			"posedge",
			requested,
			0,
			10,
			std::nullopt,
			std::nullopt,
			50,
			sample_err
		);
		assert(sample_err.empty());
		assert(samples.size() == 11);
		assert(samples[0].tick == 0);
		assert(samples[0].timestamp == 5000); // First clk rising edge at 5000ps

		// Test get_clock_edge_times
		auto edge_times = parser.get_clock_edge_times("tb_main.clk", "posedge");
		assert(edge_times.size() == 202);
		assert(edge_times[0] == 5000);
		assert(edge_times[10] == 105000);

		// Test get_signal_since_tick and get_signal_since_time
		const auto *clk_sig = parser.find_signal("tb_main.clk");
		assert(clk_sig != nullptr);
		auto clk_since = parser.get_signal_since_tick(*clk_sig, 105000, "tb_main.clk");
		assert(clk_since.has_value() && *clk_since == 10);

		const auto *rst_sig = parser.find_signal("tb_main.rst");
		assert(rst_sig != nullptr);
		auto rst_since_tick = parser.get_signal_since_tick(*rst_sig, 105000, "tb_main.clk");
		// rst deasserts at tick 2 and remains 0 at tick 10
		assert(rst_since_tick.has_value() && *rst_since_tick == 2);

		const auto *flags_sig = parser.find_signal("tb_main.uut.cpu8008.flags");
		assert(flags_sig != nullptr);
		auto flags_since = parser.get_signal_since_tick(*flags_sig, 105000, "tb_main.clk");
		// flags never transitioned from simulation start -> nullopt (init)
		assert(!flags_since.has_value());
	}

	// 2. Test vcd_file_info validator and tool
	{
		tools::vcd_file_info_validator validator;
		assert(validator.get_name() == "vcd_file_info");
		assert(validator.get_family() == "verilog");
		assert(validator.is_pure());

		agentlib::tool_context ctx;
		ctx.fs_security.add_allowed_root("tests/fixtures", agentlib::access_type::read);
		ctx.fs_security.set_working_directory(".");

		nlohmann::json args = {
			{"path", vcd_path},
			{"scope", "tb_main.uut.cpu8008"},
			{"max_signals", 50}
		};

		std::string err;
		bool valid = validator.validate_args(args, ctx, err);
		assert(valid && err.empty());

		auto tool = validator.create_tool(args);
		assert(tool != nullptr);

		std::string output = tool->execute(ctx);
		assert(output.find("VCD File Information") != std::string::npos);
		assert(output.find("tb_main.clk") != std::string::npos);
		assert(output.find("opcode") != std::string::npos);
	}

	// 3. Test vcd_wave validator and tool
	{
		tools::vcd_wave_validator validator;
		assert(validator.get_name() == "vcd_wave");
		assert(validator.get_family() == "verilog");
		assert(validator.is_pure());

		agentlib::tool_context ctx;
		ctx.fs_security.add_allowed_root("tests/fixtures", agentlib::access_type::read);
		ctx.fs_security.set_working_directory(".");

		nlohmann::json args = {
			{"path", vcd_path},
			{"signals", nlohmann::json::array({"tb_main.uut.cpu8008.opcode", "x:current_ip", "rst"})},
			{"start_tick", 0},
			{"end_tick", 5},
			{"edge", "posedge"}
		};

		std::string err;
		bool valid = validator.validate_args(args, ctx, err);
		assert(valid && err.empty());

		auto tool = validator.create_tool(args);
		assert(tool != nullptr);

		std::string output = tool->execute(ctx);
		assert(output.find("VCD Waveform") != std::string::npos);
		assert(output.find("Tick") != std::string::npos);
		assert(output.find("0x") != std::string::npos); // Formatted hex values
		assert(output.find("*(since clock)*") == std::string::npos); // Unwindowed at tick 0 should not emit since row

		// Test windowed waveform output with *(since clock)* provenance row
		{
			nlohmann::json args_win = {
				{"path", vcd_path},
				{"signals", nlohmann::json::array({"tb_main.uut.cpu8008.opcode", "rst", "tb_main.clk", "b:tb_main.uut.cpu8008.flags"})},
				{"start_tick", 10},
				{"end_tick", 15},
				{"edge", "posedge"}
			};
			std::string err_win;
			bool valid_win = validator.validate_args(args_win, ctx, err_win);
			assert(valid_win && err_win.empty());
			auto tool_win = validator.create_tool(args_win);
			assert(tool_win != nullptr);
			std::string out_win = tool_win->execute(ctx);
			assert(out_win.find("*(since clock)*") != std::string::npos);
			assert(out_win.find("| *(since clock)* | - |") != std::string::npos);
			// Verify opcode reports 9, rst reports 2, clk reports "now", and flags reports "init"
			assert(out_win.find("| 9 | 2 | now | init |") != std::string::npos);
		}

		// Test windowed waveform output with edge: "both" (Edge column present)
		{
			nlohmann::json args_both = {
				{"path", vcd_path},
				{"signals", nlohmann::json::array({"rst", "tb_main.clk"})},
				{"start_tick", 5},
				{"end_tick", 8},
				{"edge", "both"}
			};
			std::string err_both;
			bool valid_both = validator.validate_args(args_both, ctx, err_both);
			assert(valid_both && err_both.empty());
			auto tool_both = validator.create_tool(args_both);
			assert(tool_both != nullptr);
			std::string out_both = tool_both->execute(ctx);
			assert(out_both.find("*(since clock)*") != std::string::npos);
			assert(out_both.find("| *(since clock)* | - | - |") != std::string::npos);
			// In edge: "both", clk toggled at tick 5 so it reports "now", rst held since tick 3
			assert(out_both.find("| 3 | now |") != std::string::npos);
		}
	}

	// 4. Test suffix & contextual scope resolution in vcd_parser
	{
		verilog::vcd_parser parser;
		std::string err;
		bool ok = parser.parse_all(vcd_path, err);
		assert(ok);

		// Disambiguation using preceding signal's scope:
		// "tb_main.uut.cpu8008.clk" establishes scope "tb_main.uut.cpu8008".
		// Following with "mem_read_data" should resolve to "tb_main.uut.cpu8008.mem_read_data",
		// not "tb_main.uut.mem_read_data".
		const auto *first = parser.find_signal("tb_main.uut.cpu8008.clk");
		assert(first != nullptr);

		const auto *contextual = parser.find_signal("mem_read_data", first->scope);
		assert(contextual != nullptr);
		assert(contextual->full_name == "tb_main.uut.cpu8008.mem_read_data");
	}

	// 5. Test vcd_signal_labels tool, binary wildcard matching, and waveform annotation
	{
		tools::vcd_signal_labels_validator labels_validator;
		assert(labels_validator.get_name() == "vcd_signal_labels");
		assert(labels_validator.get_family() == "verilog");

		agentlib::tool_context ctx;
		ctx.fs_security.add_allowed_root("tests/fixtures", agentlib::access_type::read);
		ctx.fs_security.set_working_directory(".");

		// Register labels including both exact hex and binary with '?' wildcards
		nlohmann::json label_args = {
			{"path", vcd_path},
			{"signal", "opcode"},
			{"labels", {
				{"0x06", "LAI"},
				{"0x36", "LMA"},
				{"0xF8", "RET"},
				{"b01???01", "LDD_MASK"} // wildcard match for 0x41 (01000001) or 0x45
			}}
		};

		std::string err;
		bool valid = labels_validator.validate_args(label_args, ctx, err);
		assert(valid && err.empty());

		auto label_tool = labels_validator.create_tool(label_args);
		assert(label_tool != nullptr);
		std::string label_out = label_tool->execute(ctx);
		assert(label_out.find("LAI") != std::string::npos);

		// Now run vcd_wave and verify annotations appear
		tools::vcd_wave_validator wave_validator;
		nlohmann::json wave_args = {
			{"path", vcd_path},
			{"signals", nlohmann::json::array({"opcode"})},
			{"start_tick", 3},
			{"end_tick", 6},
			{"edge", "posedge"}
		};

		valid = wave_validator.validate_args(wave_args, ctx, err);
		assert(valid);
		auto wave_tool = wave_validator.create_tool(wave_args);
		std::string wave_out = wave_tool->execute(ctx);

		// Tick 3: 0x06 should be annotated as "0x06 (LAI)"
		assert(wave_out.find("0x06 (LAI)") != std::string::npos);
		// Tick 6: 0x36 should be annotated as "0x36 (LMA)"
		assert(wave_out.find("0x36 (LMA)") != std::string::npos);

		// Verify resolved shorthand signal mappings (expanded items only)
		assert(wave_out.find("#### Resolved Shorthand Signals (Expanded items only)") != std::string::npos);
		assert(wave_out.find("- `opcode` → `tb_main.uut.cpu8008.opcode` (8-bit)") != std::string::npos);
	}

	// 6. Test wildcard signal name matching and label listing in vcd_signal_labels
	{
		tools::vcd_signal_labels_validator labels_validator;
		agentlib::tool_context ctx;
		ctx.fs_security.add_allowed_root("tests/fixtures", agentlib::access_type::read);
		ctx.fs_security.set_working_directory(".");

		// Register labels using wildcard pattern for signal name
		nlohmann::json wildcard_args = {
			{"path", vcd_path},
			{"signal", "uut.*.A_val"},
			{"labels", {
				{"0x41", "ASCII_A"}
			}}
		};

		std::string err;
		bool valid = labels_validator.validate_args(wildcard_args, ctx, err);
		assert(valid && err.empty());

		auto tool = labels_validator.create_tool(wildcard_args);
		std::string out = tool->execute(ctx);
		assert(out.find("uut.*.A_val") != std::string::npos);

		// Now query/list labels by omitting 'labels'
		nlohmann::json list_args = {
			{"path", vcd_path}
		};
		valid = labels_validator.validate_args(list_args, ctx, err);
		assert(valid);
		auto list_tool = labels_validator.create_tool(list_args);
		std::string list_out = list_tool->execute(ctx);
		assert(list_out.find("Active Label Mappings") != std::string::npos);
		assert(list_out.find("uut.*.A_val") != std::string::npos);
		assert(list_out.find("ASCII_A") != std::string::npos);

		// Waveform check: tb_main.uut.cpu8008.A_val should match uut.*.A_val wildcard
		tools::vcd_wave_validator wave_validator;
		nlohmann::json wave_args = {
			{"path", vcd_path},
			{"signals", nlohmann::json::array({"A_val"})},
			{"start_tick", 5},
			{"end_tick", 6},
			{"edge", "posedge"}
		};

		valid = wave_validator.validate_args(wave_args, ctx, err);
		assert(valid);
		auto wave_tool = wave_validator.create_tool(wave_args);
		std::string wave_out = wave_tool->execute(ctx);
		assert(wave_out.find("0x41 (ASCII_A)") != std::string::npos);
	}

	// 7. Test zero-match warning and match note in vcd_signal_labels
	{
		tools::vcd_signal_labels_validator labels_validator;
		agentlib::tool_context ctx;
		ctx.fs_security.add_allowed_root("tests/fixtures", agentlib::access_type::read);
		ctx.fs_security.set_working_directory(".");

		// Test zero matching signals warning
		nlohmann::json zero_match_args = {
			{"path", vcd_path},
			{"signal", "*opcode_b?"},
			{"labels", {
				{"0x06", "LAI"}
			}}
		};
		std::string err;
		bool valid = labels_validator.validate_args(zero_match_args, ctx, err);
		assert(valid && err.empty());
		auto tool = labels_validator.create_tool(zero_match_args);
		std::string out = tool->execute(ctx);
		assert(out.find("> [!WARNING] Pattern `*opcode_b?` currently matches **0 signals**") != std::string::npos);

		// Test positive match confirmation note
		nlohmann::json match_args = {
			{"path", vcd_path},
			{"signal", "*opcode"},
			{"labels", {
				{"0x06", "LAI"}
			}}
		};
		valid = labels_validator.validate_args(match_args, ctx, err);
		assert(valid && err.empty());
		auto match_tool = labels_validator.create_tool(match_args);
		std::string match_out = match_tool->execute(ctx);
		assert(match_out.find("> [!NOTE] Pattern `*opcode` matches **4 signal(s)** in the VCD trace.") != std::string::npos);
	}

	// 8. Test collapse_idle and max_rows in vcd_wave
	{
		tools::vcd_wave_validator wave_validator;
		agentlib::tool_context ctx;
		ctx.fs_security.add_allowed_root("tests/fixtures", agentlib::access_type::read);
		ctx.fs_security.set_working_directory(".");

		// Test 8a: collapse_idle = true (default) collapses >= 3 unchanged cycles
		nlohmann::json collapse_args = {
			{"path", vcd_path},
			{"signals", nlohmann::json::array({"opcode"})},
			{"start_tick", 0},
			{"end_tick", 50},
			{"collapse_idle", true}
		};
		std::string err;
		bool valid = wave_validator.validate_args(collapse_args, ctx, err);
		assert(valid && err.empty());
		auto tool = wave_validator.create_tool(collapse_args);
		std::string wave_out = tool->execute(ctx);

		assert(wave_out.find("identical cycles collapsed") != std::string::npos);
		assert(wave_out.find("representing") != std::string::npos);
		assert(wave_out.find("| ... | ... | *(ticks") != std::string::npos);

		// Test 8b: collapse_idle = false emits every individual tick without collapsing
		nlohmann::json uncollapsed_args = {
			{"path", vcd_path},
			{"signals", nlohmann::json::array({"opcode"})},
			{"start_tick", 0},
			{"end_tick", 20},
			{"collapse_idle", false},
			{"max_rows", 10}
		};
		valid = wave_validator.validate_args(uncollapsed_args, ctx, err);
		assert(valid && err.empty());
		auto uncollapsed_tool = wave_validator.create_tool(uncollapsed_args);
		std::string uncollapsed_out = uncollapsed_tool->execute(ctx);

		assert(uncollapsed_out.find("identical cycles collapsed") == std::string::npos);
		assert(uncollapsed_out.find("Total Rows: 10") != std::string::npos);

		// Test 8c: max_ticks backwards-compatibility maps to max_rows
		nlohmann::json legacy_args = {
			{"path", vcd_path},
			{"signals", nlohmann::json::array({"opcode"})},
			{"start_tick", 0},
			{"end_tick", 20},
			{"collapse_idle", false},
			{"max_ticks", 5}
		};
		valid = wave_validator.validate_args(legacy_args, ctx, err);
		assert(valid && err.empty());
		auto legacy_tool = wave_validator.create_tool(legacy_args);
		std::string legacy_out = legacy_tool->execute(ctx);
		assert(legacy_out.find("Total Rows: 5") != std::string::npos);

		// Test 8d: Direct parser sample_wave with collapse_idle
		verilog::vcd_parser parser;
		bool ok = parser.parse_all(vcd_path, err);
		assert(ok && err.empty());
		std::vector<std::string> sigs = {"opcode"};
		auto collapsed_samples = parser.sample_wave(
			"tb_main.clk", "posedge", sigs, 0, 50, std::nullopt, std::nullopt, 50, err, true
		);
		assert(err.empty());
		bool found_collapsed = false;
		for (const auto &s : collapsed_samples) {
			if (s.collapsed_count > 0) {
				found_collapsed = true;
				assert(s.collapsed_count >= 3);
				assert(s.end_tick >= s.tick);
				assert(s.collapsed_count == (s.end_tick - s.tick + 1));
			}
		}
		assert(found_collapsed);
	}

	// 9. Test vcd_search tool and validator
	{
		tools::vcd_search_validator search_validator;
		assert(search_validator.get_name() == "vcd_search");
		assert(search_validator.get_family() == "verilog");
		assert(search_validator.is_pure());

		agentlib::tool_context ctx;
		ctx.fs_security.add_allowed_root("tests/fixtures", agentlib::access_type::read);
		ctx.fs_security.set_working_directory(".");

		// Test 9a: Search by Hex value ("0x06")
		nlohmann::json hex_search_args = {
			{"path", vcd_path},
			{"signal", "opcode"},
			{"value", "0x06"}
		};
		std::string err;
		bool valid = search_validator.validate_args(hex_search_args, ctx, err);
		assert(valid && err.empty());
		auto hex_tool = search_validator.create_tool(hex_search_args);
		std::string hex_out = hex_tool->execute(ctx);
		assert(hex_out.find("Matched ") != std::string::npos);
		assert(hex_out.find("Tick ranges") != std::string::npos);
		assert(hex_out.find("0x06") != std::string::npos);
		assert(hex_out.find("vcd_wave(") != std::string::npos);

		// Test 9b: Search by Label name ("LAI")
		nlohmann::json label_search_args = {
			{"path", vcd_path},
			{"signal", "opcode"},
			{"value", "LAI"}
		};
		valid = search_validator.validate_args(label_search_args, ctx, err);
		assert(valid && err.empty());
		auto label_tool = search_validator.create_tool(label_search_args);
		std::string label_out = label_tool->execute(ctx);
		assert(label_out.find("0x06 (LAI)") != std::string::npos);

		// Test 9c: Search by Binary wildcard ("b00???110" matches 0x06 and 0x36)
		nlohmann::json bin_search_args = {
			{"path", vcd_path},
			{"signal", "opcode"},
			{"value", "b00???110"}
		};
		valid = search_validator.validate_args(bin_search_args, ctx, err);
		assert(valid && err.empty());
		auto bin_tool = search_validator.create_tool(bin_search_args);
		std::string bin_out = bin_tool->execute(ctx);
		assert(bin_out.find("Matched ") != std::string::npos);

		// Test 9d: Direction and limit shortcuts ("first: true", "last: true")
		nlohmann::json first_args = {
			{"path", vcd_path},
			{"signal", "opcode"},
			{"value", "0x06"},
			{"first", true}
		};
		valid = search_validator.validate_args(first_args, ctx, err);
		assert(valid && err.empty());
		auto first_tool = search_validator.create_tool(first_args);
		std::string first_out = first_tool->execute(ctx);
		assert(first_out.find("| 1 |") != std::string::npos);
		assert(first_out.find("| 2 |") == std::string::npos); // Exactly 1 row in table

		nlohmann::json last_args = {
			{"path", vcd_path},
			{"signal", "opcode"},
			{"value", "0x06"},
			{"last", true}
		};
		valid = search_validator.validate_args(last_args, ctx, err);
		assert(valid && err.empty());
		auto last_tool = search_validator.create_tool(last_args);
		std::string last_out = last_tool->execute(ctx);
		assert(last_out.find("| 1 |") != std::string::npos);
		assert(last_out.find("| 2 |") == std::string::npos); // Exactly 1 row in table

		// Test 9e: Mutually exclusive first and last validation
		nlohmann::json invalid_args = {
			{"path", vcd_path},
			{"signal", "opcode"},
			{"value", "0x06"},
			{"first", true},
			{"last", true}
		};
		valid = search_validator.validate_args(invalid_args, ctx, err);
		assert(!valid);
		assert(err.find("Cannot specify both 'first' and 'last'") != std::string::npos);

		// Test 9f: Zero matches
		nlohmann::json no_match_args = {
			{"path", vcd_path},
			{"signal", "opcode"},
			{"value", "0xEE"}
		};
		err.clear();
		valid = search_validator.validate_args(no_match_args, ctx, err);
		assert(valid && err.empty());
		auto no_match_tool = search_validator.create_tool(no_match_args);
		std::string no_match_out = no_match_tool->execute(ctx);
		assert(no_match_out.find("No clock cycles matched") != std::string::npos);

		// Test 9g: Multi-condition AND search with map syntax
		nlohmann::json multi_map_args = {
			{"path", vcd_path},
			{"conditions", {
				{"opcode", "0x06"},
				{"opcode_data_valid", "1"}
			}}
		};
		err.clear();
		valid = search_validator.validate_args(multi_map_args, ctx, err);
		assert(valid && err.empty());
		auto multi_tool = search_validator.create_tool(multi_map_args);
		std::string multi_out = multi_tool->execute(ctx);
		assert(multi_out.find("Matched ") != std::string::npos);
		assert(multi_out.find("`opcode` == `0x06` AND `opcode_data_valid` == `1`") != std::string::npos);
		assert(multi_out.find("Matched Values") != std::string::npos);
		assert(multi_out.find("opcode=0x06") != std::string::npos);
		assert(multi_out.find("opcode_data_valid=1") != std::string::npos);
		assert(multi_out.find("vcd_wave(") != std::string::npos);
		assert(multi_out.find("\"opcode\", \"opcode_data_valid\"") != std::string::npos);

		// Test 9h: Multi-condition search with label name and integer boolean value
		nlohmann::json multi_label_args = {
			{"path", vcd_path},
			{"conditions", {
				{"opcode", "LAI"},
				{"opcode_data_valid", 1}
			}}
		};
		err.clear();
		valid = search_validator.validate_args(multi_label_args, ctx, err);
		assert(valid && err.empty());
		auto multi_label_tool = search_validator.create_tool(multi_label_args);
		std::string multi_label_out = multi_label_tool->execute(ctx);
		assert(multi_label_out.find("opcode=0x06 (LAI)") != std::string::npos);

		// Test 9i: Multi-condition mismatch (contradictory conditions yielding 0 matches)
		nlohmann::json multi_none_args = {
			{"path", vcd_path},
			{"conditions", {
				{"opcode", "0xEE"},
				{"opcode_data_valid", "1"}
			}}
		};
		err.clear();
		valid = search_validator.validate_args(multi_none_args, ctx, err);
		assert(valid && err.empty());
		auto multi_none_tool = search_validator.create_tool(multi_none_args);
		std::string multi_none_out = multi_none_tool->execute(ctx);
		assert(multi_none_out.find("No clock cycles matched") != std::string::npos);

		// Test 9j: Empty conditions map and missing signal/value validation
		nlohmann::json empty_cond_args = {
			{"path", vcd_path},
			{"conditions", nlohmann::json::object()}
		};
		err.clear();
		valid = search_validator.validate_args(empty_cond_args, ctx, err);
		assert(!valid);
		assert(err.find("Must provide either 'conditions'") != std::string::npos);

		nlohmann::json missing_all_args = {
			{"path", vcd_path}
		};
		err.clear();
		valid = search_validator.validate_args(missing_all_args, ctx, err);
		assert(!valid);
		assert(err.find("Must provide either 'conditions'") != std::string::npos);

		nlohmann::json missing_val_args = {
			{"path", vcd_path},
			{"signal", "opcode"}
		};
		err.clear();
		valid = search_validator.validate_args(missing_val_args, ctx, err);
		assert(!valid);
		assert(err.find("When 'signal' is specified, 'value' must also be specified") != std::string::npos);
	}

	// 10. vcd_signal_qualifier tool and validity masking tests
	{
		// 10a. Qualifier logic checks for 0, 1, x, z and active-low
		assert(verilog::vcd_label_manager::is_qualifier_valid("1", false));
		assert(!verilog::vcd_label_manager::is_qualifier_valid("0", false));
		assert(!verilog::vcd_label_manager::is_qualifier_valid("x", false));
		assert(!verilog::vcd_label_manager::is_qualifier_valid("z", false));
		assert(!verilog::vcd_label_manager::is_qualifier_valid("1", true));
		assert(verilog::vcd_label_manager::is_qualifier_valid("0", true));
		assert(!verilog::vcd_label_manager::is_qualifier_valid("x", true));
		assert(!verilog::vcd_label_manager::is_qualifier_valid("z", true));

		agentlib::tool_context ctx;
		ctx.fs_security.add_allowed_root("tests/fixtures", agentlib::access_type::read);
		ctx.fs_security.set_working_directory(".");

		tools::vcd_signal_qualifier_validator qual_validator;
		assert(qual_validator.get_name() == "vcd_signal_qualifier");
		assert(qual_validator.get_family() == "verilog");
		assert(!qual_validator.is_pure());
		std::string err;

		// 10b. Register single qualifier: opcode_data qualified by opcode_data_valid
		nlohmann::json reg_args = {
			{"path", vcd_path},
			{"signal", "opcode_data"},
			{"qualifier", "opcode_data_valid"}
		};
		bool valid = qual_validator.validate_args(reg_args, ctx, err);
		assert(valid && err.empty());
		auto reg_tool = qual_validator.create_tool(reg_args);
		std::string reg_out = reg_tool->execute(ctx);
		assert(reg_out.find("Bound `opcode_data`") != std::string::npos);
		assert(reg_out.find("Active-High (`1` = valid)") != std::string::npos);

		// 10c. Register warning for non-existent signal
		nlohmann::json warn_args = {
			{"path", vcd_path},
			{"signal", "non_existent_data_bus"},
			{"qualifier", "non_existent_valid_bit"}
		};
		valid = qual_validator.validate_args(warn_args, ctx, err);
		assert(valid && err.empty());
		auto warn_tool = qual_validator.create_tool(warn_args);
		std::string warn_out = warn_tool->execute(ctx);
		assert(warn_out.find("matches **0 signals**") != std::string::npos);

		// 10d. Test vcd_wave rendering with qualifier: invalid cycles should render "-"
		err.clear();
		tools::vcd_wave_validator wave_validator;
		nlohmann::json wave_args = {
			{"path", vcd_path},
			{"signals", nlohmann::json::array({"opcode_data", "opcode_data_valid"})},
			{"max_rows", 20},
			{"collapse_idle", false}
		};
		valid = wave_validator.validate_args(wave_args, ctx, err);
		assert(valid && err.empty());
		auto wave_tool = wave_validator.create_tool(wave_args);
		std::string wave_out = wave_tool->execute(ctx);
		// Verify table contains '-' when valid bit is 0
		assert(wave_out.find("| - |") != std::string::npos);

		// 10e. Test vcd_search with qualifier: search should not match cycles when invalid
		err.clear();
		tools::vcd_search_validator search_validator;
		nlohmann::json search_args = {
			{"path", vcd_path},
			{"signal", "opcode_data"},
			{"value", "0x00"}
		};
		valid = search_validator.validate_args(search_args, ctx, err);
		assert(valid && err.empty());
		auto search_tool = search_validator.create_tool(search_args);
		std::string search_out = search_tool->execute(ctx);
		// In test_dump.vcd, opcode_data is 0 at initial reset when opcode_data_valid is 0;
		// Qualifier must filter those out so they are not matched or counted as valid.

		// 10f. Batch registration with active-low prefix ("~" or "!")
		err.clear();
		nlohmann::json batch_args = {
			{"path", vcd_path},
			{"bindings", {
				{"addr_lo", "addr_lo_valid"},
				{"addr_hi", "~next_addr_hi_valid"}
			}}
		};
		valid = qual_validator.validate_args(batch_args, ctx, err);
		assert(valid && err.empty());
		auto batch_tool = qual_validator.create_tool(batch_args);
		std::string batch_out = batch_tool->execute(ctx);
		assert(batch_out.find("Registered 2 signal qualifier binding(s)") != std::string::npos);
		assert(batch_out.find("Active-Low (`0` = valid)") != std::string::npos);

		// 10g. Clear all qualifiers
		err.clear();
		nlohmann::json clear_args = {
			{"path", vcd_path},
			{"clear", true}
		};
		valid = qual_validator.validate_args(clear_args, ctx, err);
		assert(valid && err.empty());
		auto clear_tool = qual_validator.create_tool(clear_args);
		std::string clear_out = clear_tool->execute(ctx);
		assert(clear_out.find("Cleared all signal qualifiers") != std::string::npos);
		assert(clear_out.find("No signal qualifiers currently registered") != std::string::npos);
	}

	// 11. yosys_netlist_reader and vcd_signal_to_code tests
	{
		// 11a. Test source coordinate cleaning
		assert(verilog::yosys_netlist_reader::clean_src_location("sv/register/register.sv:9.21-9.26") == "sv/register/register.sv:9");
		assert(verilog::yosys_netlist_reader::clean_src_location("sv/register/register.sv:24.9-31.12") == "sv/register/register.sv:24-31");
		assert(verilog::yosys_netlist_reader::clean_src_location("sv/alu/alu.sv:0.0-0.0|sv/alu/alu.sv:24.3-53.10") == "sv/alu/alu.sv:24-53");

		const std::string netlist_path = "tests/fixtures/test_netlist.json";
		verilog::yosys_netlist_reader reader;
		std::string load_err;
		bool ok = reader.load_from_file(netlist_path, load_err);
		assert(ok && load_err.empty());
		assert(reader.net_count() > 0);
		assert(reader.cell_count() > 0);

		// 11b. Test trace_signal for A_val
		auto trace = reader.trace_signal("tb_main.uut.cpu8008.A_val");
		assert(trace.has_value());
		assert(trace->matched_net_name == "cpu8008.A_val");
		assert(trace->bit_width == 8);
		assert(!trace->hierarchy_declarations.empty());
		assert(!trace->drivers.empty());
		assert(!trace->consumers.empty());

		// Verify hierarchy declarations contain submodules
		bool has_a_reg = false;
		bool has_alu = false;
		for (const auto &decl : trace->hierarchy_declarations) {
			if (decl.hierarchical_name.find("cpu8008.A_reg.value") != std::string::npos) has_a_reg = true;
			if (decl.hierarchical_name.find("cpu8008.alu.input1") != std::string::npos) has_alu = true;
		}
		assert(has_a_reg && has_alu);

		// Format check
		std::string summary = verilog::yosys_netlist_reader::format_trace_summary(*trace);
		assert(summary.find("Signal Source Trace: `tb_main.uut.cpu8008.A_val`") != std::string::npos);
		assert(summary.find("cpu8008.A_reg.value") != std::string::npos);
		assert(summary.find("sv/register/register.sv:9") != std::string::npos);
		assert(summary.find("#### Drivers") != std::string::npos);
		assert(summary.find("#### Consumers (Sinks)") != std::string::npos);
		assert(summary.find("Reports structural net connectivity") != std::string::npos);
		assert(!reader.is_flat_gate_level());

		// 11c. Test vcd_signal_to_code validator and tool
		tools::vcd_signal_to_code_validator code_validator;
		assert(code_validator.get_name() == "vcd_signal_to_code");
		assert(code_validator.get_family() == "verilog");
		assert(code_validator.is_pure());

		agentlib::tool_context ctx;
		ctx.fs_security.add_allowed_root("tests/fixtures", agentlib::access_type::read);
		ctx.fs_security.set_working_directory(".");

		nlohmann::json tool_args = {
			{"signal", "tb_main.uut.cpu8008.A_val"},
			{"netlist", netlist_path}
		};
		std::string err;
		bool valid = code_validator.validate_args(tool_args, ctx, err);
		assert(valid && err.empty());

		auto tool = code_validator.create_tool(tool_args);
		std::string out = tool->execute(ctx);
		assert(out.find("Signal Source Trace: `tb_main.uut.cpu8008.A_val`") != std::string::npos);
		assert(out.find("sv/register/register.sv:9") != std::string::npos);
		assert(out.find("sv/alu/alu.sv") != std::string::npos);

		// 11d. Non-existent signal error handling
		nlohmann::json missing_args = {
			{"signal", "non_existent_wire_1234"},
			{"netlist", netlist_path}
		};
		err.clear();
		valid = code_validator.validate_args(missing_args, ctx, err);
		assert(valid && err.empty());
		auto missing_tool = code_validator.create_tool(missing_args);
		std::string missing_out = missing_tool->execute(ctx);
		assert(missing_out.find("not found in netlist") != std::string::npos);
	}

	// 12. Test vcd_compare tool and validator
	{
		tools::vcd_compare_validator compare_validator;
		assert(compare_validator.get_name() == "vcd_compare");
		assert(compare_validator.get_family() == "verilog");
		assert(compare_validator.is_pure());

		agentlib::tool_context ctx;
		ctx.fs_security.add_allowed_root("tests/fixtures", agentlib::access_type::read);
		ctx.fs_security.set_working_directory(".");

		const std::string org_vcd = "tests/fixtures/test_dump_org.vcd";
		const std::string mut_vcd = "tests/fixtures/test_dump_mutated.vcd";

		// 12a. Compare file with itself (Identical match)
		nlohmann::json match_args = {
			{"path_a", org_vcd},
			{"path_b", org_vcd}
		};
		std::string err;
		bool valid = compare_validator.validate_args(match_args, ctx, err);
		assert(valid && err.empty());
		auto match_tool = compare_validator.create_tool(match_args);
		std::string match_out = match_tool->execute(ctx);
		assert(match_out.find("### VCD Compare: MATCH") != std::string::npos);
		assert(match_out.find("0 differences detected") != std::string::npos);

		// 12b. Compare original vs mutated trace
		nlohmann::json diff_args = {
			{"path_a", org_vcd},
			{"path_b", mut_vcd}
		};
		err.clear();
		valid = compare_validator.validate_args(diff_args, ctx, err);
		assert(valid && err.empty());
		auto diff_tool = compare_validator.create_tool(diff_args);
		std::string diff_out = diff_tool->execute(ctx);
		assert(diff_out.find("DIVERGENCE DETECTED at Tick 10") != std::string::npos);
		assert(diff_out.find("tb_main.LED") != std::string::npos);
		assert(diff_out.find("vcd_wave(") != std::string::npos);

		// 12c. Windowed compare before divergence occurs (Ticks 0–5 should MATCH)
		nlohmann::json window_args = {
			{"path_a", org_vcd},
			{"path_b", mut_vcd},
			{"start_tick", 0},
			{"end_tick", 5}
		};
		err.clear();
		valid = compare_validator.validate_args(window_args, ctx, err);
		assert(valid && err.empty());
		auto window_tool = compare_validator.create_tool(window_args);
		std::string window_out = window_tool->execute(ctx);
		assert(window_out.find("### VCD Compare: MATCH") != std::string::npos);
		assert(window_out.find("0 differences detected") != std::string::npos);

		// 12d. Scoped comparison
		nlohmann::json scope_args = {
			{"path_a", org_vcd},
			{"path_b", mut_vcd},
			{"scope", "tb_main.uut.cpu8008.decode"}
		};
		err.clear();
		valid = compare_validator.validate_args(scope_args, ctx, err);
		assert(valid && err.empty());
		auto scope_tool = compare_validator.create_tool(scope_args);
		std::string scope_out = scope_tool->execute(ctx);
		assert(scope_out.find("DIVERGENCE DETECTED at Tick 10") != std::string::npos);
		assert(scope_out.find("tb_main.uut.cpu8008.decode.") != std::string::npos);

		// 12e. Qualifier integration (validity mismatch on qualified bus)
		tools::vcd_signal_qualifier_validator q_val;
		nlohmann::json q_args_a = {
			{"path", org_vcd},
			{"signal", "tb_main.uut.cpu8008.decode.mem_data"},
			{"qualifier", "tb_main.uut.cpu8008.decode.mem_req_en"},
			{"global", false}
		};
		bool q_ok = q_val.validate_args(q_args_a, ctx, err);
		assert(q_ok && err.empty());
		auto q_tool_a = q_val.create_tool(q_args_a);
		q_tool_a->execute(ctx);

		nlohmann::json q_args_b = {
			{"path", mut_vcd},
			{"signal", "tb_main.uut.cpu8008.decode.mem_data"},
			{"qualifier", "tb_main.uut.cpu8008.decode.mem_req_en"},
			{"global", false}
		};
		q_ok = q_val.validate_args(q_args_b, ctx, err);
		assert(q_ok && err.empty());
		auto q_tool_b = q_val.create_tool(q_args_b);
		q_tool_b->execute(ctx);

		nlohmann::json qual_compare_args = {
			{"path_a", org_vcd},
			{"path_b", mut_vcd},
			{"signals", nlohmann::json::array({"tb_main.uut.cpu8008.decode.mem_data"})},
			{"ignore_invalid", true}
		};
		err.clear();
		valid = compare_validator.validate_args(qual_compare_args, ctx, err);
		assert(valid && err.empty());
		auto qual_tool = compare_validator.create_tool(qual_compare_args);
		std::string qual_out = qual_tool->execute(ctx);
		assert(qual_out.find("Validity mismatch") != std::string::npos);
		assert(qual_out.find("- (invalid)") != std::string::npos);

		// 12f. Asymmetric qualifier sidecar unification
		// Clear qualifiers on File A, leaving qualifier on File B only
		nlohmann::json clear_a = {
			{"path", org_vcd},
			{"clear", true},
			{"global", false}
		};
		q_ok = q_val.validate_args(clear_a, ctx, err);
		assert(q_ok);
		q_val.create_tool(clear_a)->execute(ctx);

		nlohmann::json asym_args = {
			{"path_a", org_vcd},
			{"path_b", mut_vcd}
		};
		err.clear();
		valid = compare_validator.validate_args(asym_args, ctx, err);
		assert(valid && err.empty());
		auto asym_tool = compare_validator.create_tool(asym_args);
		std::string asym_out = asym_tool->execute(ctx);
		// Crucial verification: should NOT trigger false validity mismatch at tick 0!
		assert(asym_out.find("DIVERGENCE DETECTED at Tick 10") != std::string::npos);
		assert(asym_out.find("Ticks 0–9 matched identically") != std::string::npos);
		assert(asym_out.find("Applied") != std::string::npos && asym_out.find("symmetrically") != std::string::npos);

		// 12g. Missing argument validation
		nlohmann::json missing_b_args = {
			{"path_a", org_vcd}
		};
		err.clear();
		valid = compare_validator.validate_args(missing_b_args, ctx, err);
		assert(!valid);
		assert(err.find("Missing required") != std::string::npos);

		// 12h. Compare with use_qualifiers: false
		nlohmann::json raw_compare_args = {
			{"path_a", org_vcd},
			{"path_b", mut_vcd},
			{"use_qualifiers", false},
			{"start_tick", 0},
			{"end_tick", 5}
		};
		err.clear();
		valid = compare_validator.validate_args(raw_compare_args, ctx, err);
		assert(valid && err.empty());
		auto raw_tool = compare_validator.create_tool(raw_compare_args);
		std::string raw_out = raw_tool->execute(ctx);
		assert(raw_out.find("### VCD Compare: MATCH") != std::string::npos);

		// 12i. Compare edge and time filtering options
		nlohmann::json filter_args = {
			{"path_a", org_vcd},
			{"path_b", mut_vcd},
			{"edge", "both"},
			{"start_time", 0},
			{"end_time", 50000},
			{"strict", false}
		};
		err.clear();
		valid = compare_validator.validate_args(filter_args, ctx, err);
		assert(valid && err.empty());

		// 12j. Compare invalid edge parameter
		nlohmann::json bad_edge_args = {
			{"path_a", org_vcd},
			{"path_b", mut_vcd},
			{"edge", "invalid_edge"}
		};
		err.clear();
		valid = compare_validator.validate_args(bad_edge_args, ctx, err);
		assert(!valid);
		assert(err.find("Invalid 'edge' argument") != std::string::npos);
	}

	// 13. Test validator edge cases & parameter coverage
	{
		agentlib::tool_context ctx;
		ctx.fs_security.add_allowed_root("tests/fixtures", agentlib::access_type::read);
		ctx.fs_security.set_working_directory(".");
		std::string err;

		// 13a. vcd_search_validator parameter edge cases
		tools::vcd_search_validator search_val;
		nlohmann::json search_full_args = {
			{"path", vcd_path},
			{"signal", "opcode"},
			{"value", "0x06"},
			{"edge", "both"},
			{"direction", "backward"},
			{"limit", 5},
			{"start_tick", 2},
			{"end_tick", 10},
			{"start_time", 1000},
			{"end_time", 200000}
		};
		bool ok = search_val.validate_args(search_full_args, ctx, err);
		assert(ok && err.empty());

		nlohmann::json bad_dir_args = {
			{"path", vcd_path},
			{"signal", "opcode"},
			{"value", "0x06"},
			{"direction", "sideways"}
		};
		assert(!search_val.validate_args(bad_dir_args, ctx, err));
		assert(err.find("Invalid 'direction' argument") != std::string::npos);

		nlohmann::json bad_edge_search = {
			{"path", vcd_path},
			{"signal", "opcode"},
			{"value", "0x06"},
			{"edge", "falling"}
		};
		assert(!search_val.validate_args(bad_edge_search, ctx, err));

		nlohmann::json bad_cond_array = {
			{"path", vcd_path},
			{"conditions", nlohmann::json::array({"not_an_object"})}
		};
		assert(!search_val.validate_args(bad_cond_array, ctx, err));

		nlohmann::json bad_cond_empty_sig = {
			{"path", vcd_path},
			{"conditions", {{"", "val"}}}
		};
		assert(!search_val.validate_args(bad_cond_empty_sig, ctx, err));

		// 13b. vcd_wave_validator parameter edge cases
		tools::vcd_wave_validator wave_val;
		nlohmann::json wave_full_args = {
			{"path", vcd_path},
			{"signals", nlohmann::json::array({"opcode"})},
			{"edge", "negedge"},
			{"clock_signal", "tb_main.clk"},
			{"start_tick", 1},
			{"end_tick", 10},
			{"start_time", 5000},
			{"end_time", 100000},
			{"max_rows", 15},
			{"collapse_idle", false}
		};
		err.clear();
		assert(wave_val.validate_args(wave_full_args, ctx, err));

		nlohmann::json bad_edge_wave = {
			{"path", vcd_path},
			{"signals", nlohmann::json::array({"opcode"})},
			{"edge", "bad_edge"}
		};
		assert(!wave_val.validate_args(bad_edge_wave, ctx, err));

		// 13c. vcd_file_info_tool with max_signals limit
		tools::vcd_file_info_validator info_val;
		nlohmann::json info_limit_args = {
			{"path", vcd_path},
			{"scope", "tb_main.uut"},
			{"filter", "*"},
			{"max_signals", 5}
		};
		err.clear();
		assert(info_val.validate_args(info_limit_args, ctx, err));
		auto info_tool = info_val.create_tool(info_limit_args);
		std::string info_out = info_tool->execute(ctx);
		assert(info_out.find("additional signals omitted") != std::string::npos);
		tools::vcd_signal_qualifier_validator qual_val;
		nlohmann::json batch_q_args = {
			{"path", vcd_path},
			{"bindings", {
				{"opcode_byte", "opcode_valid"},
				{"next_opcode_byte", "opcode_valid"}
			}}
		};
		err.clear();
		assert(qual_val.validate_args(batch_q_args, ctx, err));
		auto batch_q_tool = qual_val.create_tool(batch_q_args);
		std::string batch_out = batch_q_tool->execute(ctx);
		assert(batch_out.find("Registered 2 signal qualifier binding(s)") != std::string::npos);

		nlohmann::json clear_all_q = {
			{"path", vcd_path},
			{"clear", true}
		};
		err.clear();
		assert(qual_val.validate_args(clear_all_q, ctx, err));
		auto clear_q_tool = qual_val.create_tool(clear_all_q);
		std::string clear_out = clear_q_tool->execute(ctx);
		assert(clear_out.find("Cleared all signal qualifiers") != std::string::npos);
	}

	// 14. Test C registration/unregistration hooks for all tools
	{
		register_vcd_file_info();
		register_vcd_wave();
		register_vcd_signal_labels();
		register_vcd_search();
		register_vcd_signal_qualifier();
		register_vcd_signal_to_code();
		register_vcd_compare();

		unregister_vcd_file_info();
		unregister_vcd_wave();
		unregister_vcd_signal_labels();
		unregister_vcd_search();
		unregister_vcd_signal_qualifier();
		unregister_vcd_signal_to_code();
		unregister_vcd_compare();
	}
	{
		tools::vcd_compare_validator comp_val;
		std::string err;

		// Strict mode failure when signal filter creates unequal signal sets
		nlohmann::json strict_args = {
			{"path_a", "tests/fixtures/test_dump_org.vcd"},
			{"path_b", "tests/fixtures/test_dump_mutated.vcd"},
			{"filter", "clk,reset,opcode_byte"},
			{"strict", true}
		};
		// They have same signals, but if we compare with filter where one signal exists only in one or no common signals:
		nlohmann::json no_common_args = {
			{"path_a", "tests/fixtures/test_dump_org.vcd"},
			{"path_b", "tests/fixtures/test_dump_mutated.vcd"},
			{"signals", {"nonexistent_signal_xyz"}}
		};
		err.clear();
		assert(comp_val.validate_args(no_common_args, global_ctx, err));
		auto no_common_tool = comp_val.create_tool(no_common_args);
		std::string no_common_out = no_common_tool->execute(global_ctx);
		assert(no_common_out.find("No common signals found") != std::string::npos);

		// Identical comparison (comparing file with itself -> MATCH)
		nlohmann::json match_args = {
			{"path_a", "tests/fixtures/test_dump_org.vcd"},
			{"path_b", "tests/fixtures/test_dump_org.vcd"},
			{"signals", {"tb_main.clk", "tb_main.reset", "tb_main.uut.opcode_byte"}},
			{"end_tick", 10}
		};
		std::string err_m;
		bool ok = comp_val.validate_args(match_args, global_ctx, err_m);
		assert(ok);
		auto match_tool = comp_val.create_tool(match_args);
		std::string match_out = match_tool->execute(global_ctx);
		assert(match_out.find("### VCD Compare: MATCH") != std::string::npos);
		assert(match_out.find("0 differences detected") != std::string::npos);
	}

	// 16. Test vcd_signal_to_code netlist error cases
	{
		tools::vcd_signal_to_code_validator s2c_val;
		std::string err;
		// Explicit nonexistent netlist
		nlohmann::json nonexist_netlist = {
			{"signal", "uut.opcode"},
			{"netlist", "tests/fixtures/does_not_exist_netlist_123.json"}
		};
		err.clear();
		assert(s2c_val.validate_args(nonexist_netlist, global_ctx, err));
		auto nonexist_tool = s2c_val.create_tool(nonexist_netlist);
		std::string nonexist_out = nonexist_tool->execute(global_ctx);
		assert(nonexist_out.find("Error loading netlist") != std::string::npos);

		// Signal not found in valid netlist
		nlohmann::json not_found_sig = {
			{"signal", "nonexistent_signal_xyz"},
			{"netlist", "tests/fixtures/test_netlist.json"}
		};
		err.clear();
		assert(s2c_val.validate_args(not_found_sig, global_ctx, err));
		auto not_found_tool = s2c_val.create_tool(not_found_sig);
		std::string not_found_out = not_found_tool->execute(global_ctx);
		assert(not_found_out.find("not found in netlist") != std::string::npos);
	}

	// 17. Test vcd_wave gtkw_output save file generation
	{
		tools::vcd_wave_validator wave_val;
		std::string err;
		std::string test_gtkw_path = "tests/fixtures/test_dump.gtkw";
		std::filesystem::remove(test_gtkw_path);

		nlohmann::json wave_gtkw_args = {
			{"path", vcd_path},
			{"signals", {"clk", "rst", "x:opcode", "b:mem_data"}},
			{"max_rows", 5},
			{"gtkw_output", test_gtkw_path}
		};
		assert(wave_val.validate_args(wave_gtkw_args, global_ctx, err));
		auto wave_tool = wave_val.create_tool(wave_gtkw_args);
		std::string out = wave_tool->execute(global_ctx);
		assert(out.find("Generated GTKWave save file") != std::string::npos);
		assert(out.find("gtkwave") != std::string::npos);

		// Verify contents of the generated GTKWave save file
		std::ifstream ifs(test_gtkw_path);
		std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
		assert(content.find("[dumpfile]") != std::string::npos);
		assert(content.find("[savefile]") != std::string::npos);
		assert(content.find("[treeopen]") != std::string::npos);
		assert(content.find("@22") != std::string::npos); // Hex
		assert(content.find("@28") != std::string::npos); // Binary / default
		assert(content.find("opcode") != std::string::npos);
		assert(content.find("clk") != std::string::npos);
		assert(content.find("[pattern_trace] 1") != std::string::npos);
		ifs.close();
		std::filesystem::remove(test_gtkw_path);
	}

	// 18. Test vcd_file_info static signal annotation and stats
	{
		tools::vcd_file_info_validator info_val;
		std::string err;

		// Default view without stats: should flag never-toggled signals with ** and show note
		nlohmann::json info_default = {
			{"path", vcd_path},
			{"scope", "tb_main.uut"},
			{"max_signals", 20}
		};
		assert(info_val.validate_args(info_default, global_ctx, err));
		auto tool_default = info_val.create_tool(info_default);
		std::string out_default = tool_default->execute(global_ctx);
		assert(out_default.find("**") != std::string::npos);
		assert(out_default.find("Signals marked with `**` had 0 transitions") != std::string::npos);

		// With stats: true: should have First Toggle and Toggle Rate columns
		nlohmann::json info_stats = {
			{"path", vcd_path},
			{"scope", "tb_main.uut"},
			{"max_signals", 20},
			{"stats", true}
		};
		err.clear();
		assert(info_val.validate_args(info_stats, global_ctx, err));
		auto tool_stats = info_val.create_tool(info_stats);
		std::string out_stats = tool_stats->execute(global_ctx);
		assert(out_stats.find("First Toggle (Tick)") != std::string::npos);
		assert(out_stats.find("Toggle Rate") != std::string::npos);
		assert(out_stats.find("never") != std::string::npos);
		assert(out_stats.find("%") != std::string::npos);
		assert(out_stats.find("Toggle Rate measures the percentage of clock cycles") != std::string::npos);

		// Clock row clarity: reference clock should report [ref clk] and 0 tick, not 0%
		nlohmann::json info_clk = {
			{"path", vcd_path},
			{"filter", "*clk*"},
			{"stats", true}
		};
		err.clear();
		assert(info_val.validate_args(info_clk, global_ctx, err));
		auto tool_clk = info_val.create_tool(info_clk);
		std::string out_clk = tool_clk->execute(global_ctx);
		assert(out_clk.find("[ref clk]") != std::string::npos);
		assert(out_clk.find("`tb_main.clk`**") == std::string::npos); // clock should never be flagged with **
		assert(out_clk.find("`tb_main.clk`") != std::string::npos);

		// Windowed clock: First Toggle should report window start tick (e.g. 8)
		nlohmann::json info_clk_windowed = {
			{"path", vcd_path},
			{"filter", "*clk*"},
			{"start_tick", 8},
			{"end_tick", 20}
		};
		err.clear();
		assert(info_val.validate_args(info_clk_windowed, global_ctx, err));
		auto tool_clk_win = info_val.create_tool(info_clk_windowed);
		std::string out_clk_win = tool_clk_win->execute(global_ctx);
		assert(out_clk_win.find("| `tb_main.clk` | 1 | wire | 8 | [ref clk] |") != std::string::npos);

		// Windowed stats: start_tick and end_tick
		nlohmann::json info_windowed = {
			{"path", vcd_path},
			{"filter", "*rst*"},
			{"start_tick", 10},
			{"end_tick", 50}
		};
		err.clear();
		assert(info_val.validate_args(info_windowed, global_ctx, err));
		auto tool_windowed = info_val.create_tool(info_windowed);
		std::string out_windowed = tool_windowed->execute(global_ctx);
		assert(out_windowed.find("Analysis Window") != std::string::npos);
		assert(out_windowed.find("Ticks 10 to 50") != std::string::npos);
		// In window [10, 50], rst remains 0 and does not toggle -> should report 'never' or 0%
		assert(out_windowed.find("never") != std::string::npos || out_windowed.find("0%") != std::string::npos);

		// Invalid window: start_tick > end_tick
		nlohmann::json info_invalid_window = {
			{"path", vcd_path},
			{"start_tick", 50},
			{"end_tick", 10}
		};
		err.clear();
		assert(!info_val.validate_args(info_invalid_window, global_ctx, err));
		assert(err.find("start_tick (50) cannot be greater than end_tick (10)") != std::string::npos);

		// Test with signal qualifier: register qualifier, verify toggle rate changes or ignores masked toggles
		tools::vcd_signal_qualifier_validator qual_val;
		nlohmann::json reg_q = {
			{"path", vcd_path},
			{"signal", "opcode_byte"},
			{"qualifier", "opcode_valid"}
		};
		err.clear();
		assert(qual_val.validate_args(reg_q, global_ctx, err));
		auto q_tool = qual_val.create_tool(reg_q);
		q_tool->execute(global_ctx);

		nlohmann::json info_qualified = {
			{"path", vcd_path},
			{"filter", "*opcode_byte*"},
			{"stats", true}
		};
		err.clear();
		assert(info_val.validate_args(info_qualified, global_ctx, err));
		auto tool_qualified = info_val.create_tool(info_qualified);
		std::string out_qualified = tool_qualified->execute(global_ctx);
		assert(out_qualified.find("opcode_byte") != std::string::npos);
		assert(out_qualified.find("%") != std::string::npos);
	}

	// 19. Test vcd_parser_cache: hits, 2-slot LRU, invalidation on file modification
	{
		auto &cache = verilog::vcd_parser_cache::get_instance();
		cache.clear();
		assert(cache.size() == 0);

		std::string err;
		auto p1 = cache.get_or_parse(vcd_path, err);
		assert(p1 != nullptr && err.empty());
		assert(cache.size() == 1);

		// Cache hit returns identical shared_ptr
		auto p1_hit = cache.get_or_parse(vcd_path, err);
		assert(p1_hit == p1);
		assert(cache.size() == 1);

		// Insert second file: both fit in 2-slot LRU
		const std::string vcd_path_b = "tests/fixtures/test_dump_mutated.vcd";
		auto p2 = cache.get_or_parse(vcd_path_b, err);
		assert(p2 != nullptr && err.empty());
		assert(cache.size() == 2);
		assert(p2 != p1);

		// Insert third file: evicts oldest (vcd_path)
		const std::string vcd_path_c = "tests/fixtures/test_dump_org.vcd";
		auto p3 = cache.get_or_parse(vcd_path_c, err);
		assert(p3 != nullptr && err.empty());
		assert(cache.size() == 2);

		// p2 and p3 should now be cached
		auto p2_hit = cache.get_or_parse(vcd_path_b, err);
		assert(p2_hit == p2);

		auto p3_hit = cache.get_or_parse(vcd_path_c, err);
		assert(p3_hit == p3);

		// Test invalidation when file is modified:
		const std::string tmp_vcd = "tests/fixtures/test_cache_tmp.vcd";
		std::filesystem::copy_file(vcd_path, tmp_vcd, std::filesystem::copy_options::overwrite_existing);

		auto p_tmp1 = cache.get_or_parse(tmp_vcd, err);
		assert(p_tmp1 != nullptr);

		// Modify file on disk (append a comment line)
		{
			std::ofstream ofs(tmp_vcd, std::ios::app);
			ofs << "\n$comment modified for cache test $end\n";
		}

		// Cache should detect modification and re-parse
		auto p_tmp2 = cache.get_or_parse(tmp_vcd, err);
		assert(p_tmp2 != nullptr);
		assert(p_tmp2 != p_tmp1);

		std::filesystem::remove(tmp_vcd);
		cache.clear();
		assert(cache.size() == 0);
	}

	// 20. Test overlapping label pattern override (*opcode vs *opcode*) and stale VCD warning
	{
		auto &lbl_mgr = verilog::vcd_label_manager::get_instance();
		lbl_mgr.clear_all();

		std::string tmp_dir = std::format("tests/fixtures/tmp_stale_vcd_{}", (long)getpid());
		std::filesystem::create_directories(tmp_dir);
		std::string stale_vcd = tmp_dir + "/dump.vcd";
		std::filesystem::copy_file(vcd_path, stale_vcd, std::filesystem::copy_options::overwrite_existing);

		bool saved = false;
		std::string msg;
		lbl_mgr.set_labels(stale_vcd, "*opcode", {{"0x36", "LDI"}}, false, saved, msg);
		assert(lbl_mgr.find_label(stale_vcd, "tb_main.uut.opcode", "0x36") == "LDI");

		// Register broader overlapping pattern *opcode* with updated label for 0x36
		lbl_mgr.set_labels(stale_vcd, "*opcode*", {{"0x36", "LLI"}}, false, saved, msg);
		assert(lbl_mgr.find_label(stale_vcd, "tb_main.uut.opcode", "0x36") == "LLI");

		// Backdate VCD mtime by 60 seconds and create a newer .sv file in the same directory
		auto now_ft = std::filesystem::file_time_type::clock::now();
		std::filesystem::last_write_time(stale_vcd, now_ft - std::chrono::seconds(60));
		std::string newer_sv = tmp_dir + "/alu.sv";
		{
			std::ofstream ofs(newer_sv);
			ofs << "module alu; endmodule\n";
		}

		tools::vcd_file_info_validator info_val;
		nlohmann::json stale_args = {{"path", stale_vcd}};
		std::string err;
		assert(info_val.validate_args(stale_args, global_ctx, err));
		auto stale_tool = info_val.create_tool(stale_args);
		std::string stale_out = stale_tool->execute(global_ctx);
		assert(stale_out.find("[!WARNING]") != std::string::npos);
		assert(stale_out.find("alu.sv") != std::string::npos);

		std::error_code ec_rm;
		std::filesystem::remove_all(tmp_dir, ec_rm);
		lbl_mgr.clear_all();
	}

	// 21. Test verilog_simulate tool (testbench auto-discovery, $readmemh staging, VCD cache warming, and next-step hints)
	if (!fs_utils::find_executable("verilator").empty()) {
		register_verilog_simulate();

		std::string sim_dir = std::format("tests/fixtures/tmp_verilog_sim_{}", (long)getpid());
		std::filesystem::create_directories(sim_dir + "/sv/ram");
		std::filesystem::create_directories(sim_dir + "/tb");

		{
			std::ofstream hex_ofs(sim_dir + "/sv/ram/ram_init.hex");
			hex_ofs << "42\nA5\n";
		}
		{
			std::ofstream dut_ofs(sim_dir + "/sv/dut.sv");
			dut_ofs << "`timescale 1ns/1ps\n"
			        << "module dut(input logic clk, input logic rst, output logic [7:0] out_val);\n"
			        << "    logic [7:0] mem [0:1];\n"
			        << "    initial $readmemh(\"ram_init.hex\", mem);\n"
			        << "    always_ff @(posedge clk) begin\n"
			        << "        if (rst) out_val <= mem[0];\n"
			        << "        else out_val <= mem[1];\n"
			        << "    end\n"
			        << "endmodule\n";
		}
		{
			std::ofstream tb_ofs(sim_dir + "/tb/tb_dut.sv");
			tb_ofs << "`timescale 1ns/1ps\n"
			       << "module tb_dut;\n"
			       << "    logic clk = 0;\n"
			       << "    logic rst = 1;\n"
			       << "    logic [7:0] out_val;\n"
			       << "    dut uut(.clk(clk), .rst(rst), .out_val(out_val));\n"
			       << "    always #5 clk = ~clk;\n"
			       << "    initial begin\n"
			       << "        $dumpfile(\"sim_out.vcd\");\n"
			       << "        $dumpvars(0, tb_dut);\n"
			       << "        #12 rst = 0;\n"
			       << "        #20;\n"
			       << "        if (out_val == 8'ha5) $display(\"OUT=%h PASS\", out_val);\n"
			       << "        else $display(\"OUT=%h FAIL\", out_val);\n"
			       << "        $finish;\n"
			       << "    end\n"
			       << "endmodule\n";
		}

		agentlib::tool_context sim_ctx;
		sim_ctx.fs_security.add_allowed_root(sim_dir, agentlib::access_type::read);
		sim_ctx.fs_security.add_allowed_root(sim_dir, agentlib::access_type::write);
		sim_ctx.fs_security.set_working_directory(sim_dir);

		verilog::vcd_parser_cache::get_instance().clear();

		tools::verilog_simulate_validator sim_val;
		nlohmann::json sim_args = nlohmann::json::object();
		std::string err;
		assert(sim_val.validate_args(sim_args, sim_ctx, err) && err.empty());
		auto sim_tool = sim_val.create_tool(sim_args);
		std::string sim_out = sim_tool->execute(sim_ctx);

		assert(sim_out.find("SUCCESS") != std::string::npos);
		assert(sim_out.find("OUT=a5 PASS") != std::string::npos);
		assert(sim_out.find("Generated VCD Summary (`sim_out.vcd`)") != std::string::npos);
		assert(sim_out.find("Passing Baseline Saved**: `sim_out.lastpass.vcd`") != std::string::npos);
		assert(sim_out.find("vcd_file_info(path=\"sim_out.vcd\"") != std::string::npos);
		assert(sim_out.find("vcd_wave(path=\"sim_out.vcd\"") != std::string::npos);
		assert(sim_out.find("sim_out.previous.vcd") == std::string::npos);
		assert(std::filesystem::exists(sim_dir + "/sim_out.lastpass.vcd"));
		assert(!std::filesystem::exists(sim_dir + "/sim_out.previous.vcd"));
		assert(verilog::vcd_parser_cache::get_instance().size() == 1);

		// Re-run simulation without changing DUT: verify automatic comparison reports IDENTICAL and saves a vcd_compare call
		auto sim_tool_identical = sim_val.create_tool(sim_args);
		std::string sim_out_identical = sim_tool_identical->execute(sim_ctx);
		assert(sim_out_identical.find("Comparison vs. Previous (`sim_out.previous.vcd`)**: **IDENTICAL**") != std::string::npos);
		assert(sim_out_identical.find("waveforms are **IDENTICAL**") != std::string::npos);
		assert(sim_out_identical.find("no `vcd_compare` needed") != std::string::npos);

		// Mutate DUT so the next simulation run produces a failing result, and verify both
		// sim_out.previous.vcd and sim_out.lastpass.vcd are preserved, DIVERGED is reported, and vcd_compare is suggested
		{
			std::ofstream dut_ofs(sim_dir + "/sv/dut.sv");
			dut_ofs << "`timescale 1ns/1ps\n"
			        << "module dut(input logic clk, input logic rst, output logic [7:0] out_val);\n"
			        << "    always_ff @(posedge clk) begin\n"
			        << "        if (rst) out_val <= 8'h42;\n"
			        << "        else out_val <= 8'hff;\n"
			        << "    end\n"
			        << "endmodule\n";
		}

		auto sim_tool_2 = sim_val.create_tool(sim_args);
		std::string sim_out_2 = sim_tool_2->execute(sim_ctx);
		assert(sim_out_2.find("OUT=ff FAIL") != std::string::npos);
		assert(std::filesystem::exists(sim_dir + "/sim_out.previous.vcd"));
		assert(std::filesystem::exists(sim_dir + "/sim_out.lastpass.vcd"));
		assert(sim_out_2.find("Comparison vs. Previous (`sim_out.previous.vcd`)**: **DIVERGED**") != std::string::npos);
		assert(sim_out_2.find("To compare the current result to this file use `vcd_compare(path_a=\"sim_out.previous.vcd\", path_b=\"sim_out.vcd\", scope=\"tb_dut.uut\")`") != std::string::npos);
		assert(sim_out_2.find("To compare to the last passing run, use `vcd_compare(path_a=\"sim_out.lastpass.vcd\", path_b=\"sim_out.vcd\", scope=\"tb_dut.uut\")`") != std::string::npos);

		// Run a 3rd simulation that also fails (8'hee); verify sim_out.lastpass.vcd STILL holds the passing 8'ha5 trace
		{
			std::ofstream dut_ofs(sim_dir + "/sv/dut.sv");
			dut_ofs << "`timescale 1ns/1ps\n"
			        << "module dut(input logic clk, input logic rst, output logic [7:0] out_val);\n"
			        << "    always_ff @(posedge clk) begin\n"
			        << "        if (rst) out_val <= 8'h42;\n"
			        << "        else out_val <= 8'hee;\n"
			        << "    end\n"
			        << "endmodule\n";
		}

		auto sim_tool_3 = sim_val.create_tool(sim_args);
		std::string sim_out_3 = sim_tool_3->execute(sim_ctx);
		assert(sim_out_3.find("OUT=ee FAIL") != std::string::npos);
		assert(sim_out_3.find("Comparison vs. Previous (`sim_out.previous.vcd`)**: **DIVERGED**") != std::string::npos);
		assert(sim_out_3.find("Comparison vs. Last Passing (`sim_out.lastpass.vcd`)**: **DIVERGED**") != std::string::npos);
		assert(sim_out_3.find("To compare to the last passing run, use `vcd_compare(path_a=\"sim_out.lastpass.vcd\", path_b=\"sim_out.vcd\", scope=\"tb_dut.uut\")`") != std::string::npos);
		tools::vcd_compare_validator cmp_val;
		nlohmann::json cmp_args = {
			{"path_a", "sim_out.lastpass.vcd"},
			{"path_b", "sim_out.vcd"},
			{"scope", "tb_dut.uut"}
		};
		assert(cmp_val.validate_args(cmp_args, sim_ctx, err) && err.empty());
		auto cmp_tool = cmp_val.create_tool(cmp_args);
		std::string cmp_out = cmp_tool->execute(sim_ctx);
		assert(cmp_out.find("0xA5") != std::string::npos);
		assert(cmp_out.find("0xEE") != std::string::npos);

		// Run a 4th simulation where the bug is fixed (back to 8'ha5 PASS); verify that it compares against
		// BOTH the failed previous run (DIVERGED) AND the prior lastpass baseline (IDENTICAL) before updating lastpass!
		{
			std::ofstream dut_ofs(sim_dir + "/sv/dut.sv");
			dut_ofs << "`timescale 1ns/1ps\n"
			        << "module dut(input logic clk, input logic rst, output logic [7:0] out_val);\n"
			        << "    always_ff @(posedge clk) begin\n"
			        << "        if (rst) out_val <= 8'h42;\n"
			        << "        else out_val <= 8'ha5;\n"
			        << "    end\n"
			        << "endmodule\n";
		}
		auto sim_tool_4 = sim_val.create_tool(sim_args);
		std::string sim_out_4 = sim_tool_4->execute(sim_ctx);
		assert(sim_out_4.find("OUT=a5 PASS") != std::string::npos);
		assert(sim_out_4.find("Comparison vs. Previous (`sim_out.previous.vcd`)**: **DIVERGED**") != std::string::npos);
		assert(sim_out_4.find("Comparison vs. Last Passing (`sim_out.lastpass.vcd`)**: **IDENTICAL**") != std::string::npos);
		assert(sim_out_4.find("Prior passing baseline (`sim_out.lastpass.vcd`) waveform is **IDENTICAL** to the current result") != std::string::npos);

		// Verify vcd_file_info resolves the VCD filename from the testbench's $dumpfile("sim_out.vcd")
		// when path is omitted OR when a mismatched prefix like "sim/sim_out.vcd" is passed
		tools::vcd_file_info_validator info_val;
		nlohmann::json auto_path_args = nlohmann::json::object();
		assert(info_val.validate_args(auto_path_args, sim_ctx, err) && err.empty());
		auto info_tool = info_val.create_tool(auto_path_args);
		std::string info_out = info_tool->execute(sim_ctx);
		assert(info_out.find("sim_out.vcd") != std::string::npos);

		nlohmann::json prefixed_path_args = {{"path", "sim/sim_out.vcd"}};
		assert(info_val.validate_args(prefixed_path_args, sim_ctx, err) && err.empty());

		unregister_verilog_simulate();
		std::error_code ec_rm;
		std::filesystem::remove_all(sim_dir, ec_rm);
	}

	// 22. Test one-shot contextual hints in vcd_wave (_valid qualifier priority over state/opcode/instr/fsm labels, shown once each)
	{
		auto &lbl_mgr = verilog::vcd_label_manager::get_instance();
		lbl_mgr.clear_all();
		std::filesystem::remove(vcd_path + ".labels.json");
		std::filesystem::remove("tests/fixtures/vcd_labels.json");
		std::filesystem::remove("vcd_labels.json");

		tools::vcd_wave_validator wave_val;
		nlohmann::json wave_args = {
			{"path", vcd_path},
			{"signals", nlohmann::json::array({"opcode_data", "opcode_data_valid", "opcode"})},
			{"max_rows", 10}
		};
		std::string err;
		assert(wave_val.validate_args(wave_args, global_ctx, err) && err.empty());

		// Call 1: both _valid pair and opcode/state signals are present and unregistered -> only Hint 1 (_valid qualifier) is shown
		auto wave_tool_1 = wave_val.create_tool(wave_args);
		std::string out_1 = wave_tool_1->execute(global_ctx);
		assert(out_1.find("vcd_signal_qualifier(path=\"") != std::string::npos);
		assert(out_1.find("signal=\"opcode_data\", qualifier=\"opcode_data_valid\"") != std::string::npos);
		assert(out_1.find("vcd_signal_labels(") == std::string::npos);

		// Call 2: Hint 1 was already shown once, so now Hint 2 (symbolic labels for opcode_data) is shown
		auto wave_tool_2 = wave_val.create_tool(wave_args);
		std::string out_2 = wave_tool_2->execute(global_ctx);
		assert(out_2.find("vcd_signal_qualifier(") == std::string::npos);
		assert(out_2.find("vcd_signal_labels(path=\"") != std::string::npos);
		assert(out_2.find("signal=\"opcode_data\"") != std::string::npos);

		// Call 3: Both hints have been shown once -> neither hint is repeated
		auto wave_tool_3 = wave_val.create_tool(wave_args);
		std::string out_3 = wave_tool_3->execute(global_ctx);
		assert(out_3.find("vcd_signal_qualifier(") == std::string::npos);
		assert(out_3.find("vcd_signal_labels(") == std::string::npos);
	}

	// 23. Test project-global vcd_labels.json (default global: true) and local override (global: false)
	{
		auto &lbl_mgr = verilog::vcd_label_manager::get_instance();
		lbl_mgr.clear_all();
		std::filesystem::remove(vcd_path + ".labels.json");
		std::filesystem::remove("tests/fixtures/test_dump_org.vcd.labels.json");
		std::filesystem::remove("tests/fixtures/vcd_labels.json");

		tools::vcd_signal_labels_validator lbl_val;
		tools::vcd_signal_qualifier_validator qual_val;
		std::string err;

		// Register global label (default global: true) on test_dump.vcd
		nlohmann::json global_lbl_args = {
			{"path", vcd_path},
			{"signal", "*opcode"},
			{"labels", {{"0x06", "GLOBAL_LAI"}}}
		};
		assert(lbl_val.validate_args(global_lbl_args, global_ctx, err) && err.empty());
		std::string g_out = lbl_val.create_tool(global_lbl_args)->execute(global_ctx);
		assert(g_out.find("Saved to project-global vcd_labels.json.") != std::string::npos);
		assert(std::filesystem::exists("tests/fixtures/vcd_labels.json"));
		assert(!std::filesystem::exists(vcd_path + ".labels.json"));

		// Register global qualifier (default global: true)
		nlohmann::json global_qual_args = {
			{"path", vcd_path},
			{"signal", "opcode_data"},
			{"qualifier", "opcode_data_valid"}
		};
		assert(qual_val.validate_args(global_qual_args, global_ctx, err) && err.empty());
		std::string gq_out = qual_val.create_tool(global_qual_args)->execute(global_ctx);
		assert(gq_out.find("Saved to project-global vcd_labels.json.") != std::string::npos);

		// Clear in-memory cache and verify a DIFFERENT VCD file in the project (test_dump_org.vcd)
		// automatically inherits both the global label and the global qualifier from vcd_labels.json!
		lbl_mgr.clear_all();
		assert(lbl_mgr.find_label("tests/fixtures/test_dump_org.vcd", "tb_main.uut.cpu8008.opcode", "0x06") == "GLOBAL_LAI");
		assert(lbl_mgr.find_qualifier("tests/fixtures/test_dump_org.vcd", "opcode_data").first == "opcode_data_valid");

		// Now register a local override (global: false) on test_dump_org.vcd and verify it overrides the global label
		// for test_dump_org.vcd while leaving test_dump.vcd with GLOBAL_LAI
		nlohmann::json local_lbl_args = {
			{"path", "tests/fixtures/test_dump_org.vcd"},
			{"signal", "*opcode"},
			{"labels", {{"0x06", "LOCAL_LAI"}}},
			{"global", false}
		};
		assert(lbl_val.validate_args(local_lbl_args, global_ctx, err) && err.empty());
		std::string l_out = lbl_val.create_tool(local_lbl_args)->execute(global_ctx);
		assert(l_out.find("Saved to sidecar file.") != std::string::npos);
		assert(std::filesystem::exists("tests/fixtures/test_dump_org.vcd.labels.json"));

		assert(lbl_mgr.find_label("tests/fixtures/test_dump_org.vcd", "tb_main.uut.cpu8008.opcode", "0x06") == "LOCAL_LAI");
		assert(lbl_mgr.find_label(vcd_path, "tb_main.uut.cpu8008.opcode", "0x06") == "GLOBAL_LAI");
	}

	// 24. Test automatic SystemVerilog enum discovery (typedef enum & inline enum) and zero-config vcd_wave annotation
	{
		// Verify discovery on real examples/test8008 directory
		auto test8008_enums = verilog::verilog_module_scanner::discover_enum_signal_labels("examples/test8008");
		assert(test8008_enums.contains("*sequencer.state"));
		assert(test8008_enums.contains("*sequencer.next_state"));
		assert(test8008_enums["*sequencer.state"]["0x1"] == "FETCH");
		assert(test8008_enums["*sequencer.state"]["0x2"] == "DECODE");
		assert(test8008_enums["*sequencer.state"]["0x4"] == "RETIRE");
		assert(test8008_enums["*sequencer.state"]["0x8"] == "HALT");
		assert(test8008_enums["*sequencer.next_state"]["0x1"] == "FETCH");
		assert(test8008_enums["*sequencer.next_state"]["0x2"] == "DECODE");
		assert(test8008_enums["*sequencer.next_state"]["0x4"] == "RETIRE");
		assert(test8008_enums["*sequencer.next_state"]["0x8"] == "HALT");

		// Create an isolated temp project with typedef enum + inline enum + copy of test_dump.vcd (and NO sidecar files)
		std::filesystem::path tmp_proj = std::filesystem::temp_directory_path() / "turbostar_test_sv_enum_auto";
		std::filesystem::remove_all(tmp_proj);
		std::filesystem::create_directories(tmp_proj / "sv");

		{
			std::ofstream seq_f(tmp_proj / "sv" / "sequencer.sv");
			seq_f << "typedef enum logic [2:0] { DECODE = 3'b001, RETIRE = 3'b010, HALT = 3'b100 } state_t;\n"
			      << "module sequencer_mod(input logic clk);\n"
			      << "    state_t state;\n"
			      << "    state_t next_state;\n"
			      << "    enum logic [1:0] { IDLE, BUSY = 2, DONE } inline_fsm;\n"
			      << "endmodule\n"
			      << "module cpu_top(input logic clk);\n"
			      << "    sequencer_mod sequencer(.clk(clk));\n"
			      << "endmodule\n";
		}
		std::filesystem::copy_file("examples/test8008/dump.vcd", tmp_proj / "dump.vcd", std::filesystem::copy_options::overwrite_existing);
		auto &lbl_mgr = verilog::vcd_label_manager::get_instance();
		lbl_mgr.clear_all();

		agentlib::tool_context enum_ctx;
		enum_ctx.fs_security.add_allowed_root(tmp_proj.string(), agentlib::access_type::read);
		enum_ctx.fs_security.set_working_directory(tmp_proj.string());

		tools::vcd_wave_validator wave_val;
		std::string err;
		nlohmann::json wave_args = {
			{"path", (tmp_proj / "dump.vcd").string()},
			{"signals", nlohmann::json::array({"tb_main.uut.cpu8008.sequencer.state", "tb_main.uut.cpu8008.sequencer.next_state"})},
			{"start_tick", 0},
			{"end_tick", 10}
		};
		assert(wave_val.validate_args(wave_args, enum_ctx, err) && err.empty());
		std::string w_out = wave_val.create_tool(wave_args)->execute(enum_ctx);

		// Verify DECODE and RETIRE are automatically annotated in vcd_wave without any vcd_signal_labels call!
		assert(w_out.find("0x1 (DECODE)") != std::string::npos);
		assert(w_out.find("0x2 (RETIRE)") != std::string::npos);
		// And because labels were auto-discovered, the one-shot vcd_signal_labels hint is not wasted on sequencer.state
		assert(w_out.find("vcd_signal_labels") == std::string::npos);

		// Also verify inline enum auto-increment values (IDLE=0x0, BUSY=0x2, DONE=0x3)
		assert(lbl_mgr.find_label((tmp_proj / "dump.vcd").string(), "tb_main.sequencer.inline_fsm", "0x00") == "IDLE");
		assert(lbl_mgr.find_label((tmp_proj / "dump.vcd").string(), "tb_main.sequencer.inline_fsm", "0x02") == "BUSY");
		assert(lbl_mgr.find_label((tmp_proj / "dump.vcd").string(), "tb_main.sequencer.inline_fsm", "0x03") == "DONE");

		// Now modify sequencer.sv by inserting new enum values in the middle (WITHOUT calling lbl_mgr.clear_all())
		// and verify vcd_wave and find_label automatically revalidate and rescan the updated enum values!
		{
			std::ofstream seq_f(tmp_proj / "sv" / "sequencer.sv", std::ios::trunc);
			seq_f << "typedef enum logic [2:0] { IDLE_0, FETCH_MID, DECODE, RETIRE, HALT } state_t;\n"
			      << "module sequencer_mod(input logic clk);\n"
			      << "    state_t state;\n"
			      << "    state_t next_state;\n"
			      << "    enum logic [2:0] { IDLE, PREP_1, PREP_2, BUSY, DONE } inline_fsm;\n"
			      << "endmodule\n"
			      << "module cpu_top(input logic clk);\n"
			      << "    sequencer_mod sequencer(.clk(clk));\n"
			      << "endmodule\n";
		}

		// Without clear_all(), vcd_wave and find_label must reflect the newly inserted middle enum values:
		// state_t: 0x0=IDLE_0, 0x1=FETCH_MID, 0x2=DECODE, 0x3=RETIRE, 0x4=HALT
		// inline_fsm: 0x0=IDLE, 0x1=PREP_1, 0x2=PREP_2, 0x3=BUSY, 0x4=DONE
		std::string w_out_rescanned = wave_val.create_tool(wave_args)->execute(enum_ctx);
		assert(w_out_rescanned.find("0x1 (FETCH_MID)") != std::string::npos);
		assert(w_out_rescanned.find("0x2 (DECODE)") != std::string::npos);
		assert(lbl_mgr.find_label((tmp_proj / "dump.vcd").string(), "tb_main.sequencer.inline_fsm", "0x01") == "PREP_1");
		assert(lbl_mgr.find_label((tmp_proj / "dump.vcd").string(), "tb_main.sequencer.inline_fsm", "0x02") == "PREP_2");
		assert(lbl_mgr.find_label((tmp_proj / "dump.vcd").string(), "tb_main.sequencer.inline_fsm", "0x03") == "BUSY");
		assert(lbl_mgr.find_label((tmp_proj / "dump.vcd").string(), "tb_main.sequencer.inline_fsm", "0x04") == "DONE");

		// Also verify that if enum values were previously saved in vcd_labels.json and then new values are inserted
		// in the middle of the RTL enum, the saved labels are reconciled against the updated RTL enum
		bool saved = false;
		std::string save_msg;
		lbl_mgr.set_labels(
			(tmp_proj / "dump.vcd").string(),
			"*sequencer.state",
			{{"0x0", "IDLE_0"}, {"0x1", "FETCH_MID"}, {"0x2", "DECODE"}, {"0x3", "RETIRE"}, {"0x4", "HALT"}},
			false,
			saved,
			save_msg,
			true
		);
		{
			std::ofstream seq_f(tmp_proj / "sv" / "sequencer.sv", std::ios::trunc);
			seq_f << "typedef enum logic [2:0] { IDLE_0, EXTRA_A, EXTRA_B, FETCH_MID, DECODE, RETIRE, HALT } state_t;\n"
			      << "module sequencer_mod(input logic clk);\n"
			      << "    state_t state;\n"
			      << "    state_t next_state;\n"
			      << "endmodule\n"
			      << "module cpu_top(input logic clk);\n"
			      << "    sequencer_mod sequencer(.clk(clk));\n"
			      << "endmodule\n";
		}
		assert(lbl_mgr.find_label((tmp_proj / "dump.vcd").string(), "tb_main.uut.cpu8008.sequencer.state", "0x01") == "EXTRA_A");
		assert(lbl_mgr.find_label((tmp_proj / "dump.vcd").string(), "tb_main.uut.cpu8008.sequencer.state", "0x02") == "EXTRA_B");
		assert(lbl_mgr.find_label((tmp_proj / "dump.vcd").string(), "tb_main.uut.cpu8008.sequencer.state", "0x03") == "FETCH_MID");
		assert(lbl_mgr.find_label((tmp_proj / "dump.vcd").string(), "tb_main.uut.cpu8008.sequencer.state", "0x04") == "DECODE");

		std::filesystem::remove_all(tmp_proj);
	}

	// 25. Test verilog_configure_testbench and module-level / hierarchical verilog_simulate
	{
		register_verilog_configure_testbench();
		unregister_verilog_configure_testbench();

		std::filesystem::path tmp_proj = std::format("tests/unit/tmp_mod_sim_{}", (long)getpid());
		std::filesystem::remove_all(tmp_proj);
		std::filesystem::create_directories(tmp_proj / "sv" / "alu");
		std::filesystem::create_directories(tmp_proj / "sv" / "cpu");
		std::filesystem::create_directories(tmp_proj / "tb");

		// Leaf module: alu_mod in sv/alu/alu.sv
		{
			std::ofstream f(tmp_proj / "sv" / "alu" / "alu.sv");
			f << "module alu_mod(input logic clk, input logic [7:0] a, b, output logic [7:0] y);\n"
			  << "    always_ff @(posedge clk) y <= a + b;\n"
			  << "endmodule\n";
		}
		// Parent module: cpu_mod in sv/cpu/cpu.sv instantiating alu_mod
		{
			std::ofstream f(tmp_proj / "sv" / "cpu" / "cpu.sv");
			f << "module cpu_mod(input logic clk, input logic [7:0] in_a, in_b, output logic [7:0] out_y);\n"
			  << "    alu_mod alu_inst(.clk(clk), .a(in_a), .b(in_b), .y(out_y));\n"
			  << "endmodule\n";
		}
		// Testbench 1: tb/tb_alu.sv (uses $dumpfile("dump.vcd") - intentionally colliding with tb_cpu_extreme.sv)
		{
			std::ofstream f(tmp_proj / "tb" / "tb_alu.sv");
			f << "`timescale 1ns/1ps\n"
			  << "module tb_alu;\n"
			  << "    logic clk = 0;\n"
			  << "    always #5 clk = ~clk;\n"
			  << "    logic [7:0] a = 8'd3, b = 8'd4, y;\n"
			  << "    alu_mod uut(.clk(clk), .a(a), .b(b), .y(y));\n"
			  << "    initial begin\n"
			  << "        $dumpfile(\"dump.vcd\");\n"
			  << "        $dumpvars(0, tb_alu);\n"
			  << "        #20;\n"
			  << "        if (y == 8'd7) $display(\"ALU TEST PASSED\");\n"
			  << "        else $display(\"ALU TEST FAILED\");\n"
			  << "        $finish;\n"
			  << "    end\n"
			  << "endmodule\n";
		}
		// Testbench 2: tb/tb_cpu.sv (uses unique $dumpfile("dump_cpu.vcd"))
		{
			std::ofstream f(tmp_proj / "tb" / "tb_cpu.sv");
			f << "`timescale 1ns/1ps\n"
			  << "module tb_cpu;\n"
			  << "    logic clk = 0;\n"
			  << "    always #5 clk = ~clk;\n"
			  << "    logic [7:0] a = 8'd10, b = 8'd20, y;\n"
			  << "    cpu_mod uut(.clk(clk), .in_a(a), .in_b(b), .out_y(y));\n"
			  << "    initial begin\n"
			  << "        $dumpfile(\"dump_cpu.vcd\");\n"
			  << "        $dumpvars(0, tb_cpu);\n"
			  << "        #20;\n"
			  << "        if (y == 8'd30) $display(\"CPU TEST PASSED\");\n"
			  << "        else $display(\"CPU TEST FAILED\");\n"
			  << "        $finish;\n"
			  << "    end\n"
			  << "endmodule\n";
		}
		// Testbench 3: tb/tb_cpu_extreme.sv (uses colliding $dumpfile("dump.vcd"), configured as "extreme")
		{
			std::ofstream f(tmp_proj / "tb" / "tb_cpu_extreme.sv");
			f << "`timescale 1ns/1ps\n"
			  << "module tb_cpu_extreme;\n"
			  << "    logic clk = 0;\n"
			  << "    always #5 clk = ~clk;\n"
			  << "    logic [7:0] a = 8'd100, b = 8'd50, y;\n"
			  << "    cpu_mod uut(.clk(clk), .in_a(a), .in_b(b), .out_y(y));\n"
			  << "    initial begin\n"
			  << "        $dumpfile(\"dump.vcd\");\n"
			  << "        $dumpvars(0, tb_cpu_extreme);\n"
			  << "        #20;\n"
			  << "        if (y == 8'd150) $display(\"EXTREME CPU TEST PASSED\");\n"
			  << "        else $display(\"EXTREME CPU TEST FAILED\");\n"
			  << "        $finish;\n"
			  << "    end\n"
			  << "endmodule\n";
		}

		// Verify scanner hierarchy discovery (bottom-up: alu_mod first, cpu_mod second)
		auto rtl_files = verilog::verilog_module_scanner::find_synthesizable_files(tmp_proj / "sv");
		auto hier = verilog::verilog_module_scanner::discover_submodule_hierarchy("cpu_mod", rtl_files);
		assert(hier.size() == 2);
		assert(hier[0] == "alu_mod");
		assert(hier[1] == "cpu_mod");

		auto inst_mods = verilog::verilog_module_scanner::discover_instantiated_modules(tmp_proj / "tb" / "tb_alu.sv", rtl_files);
		assert(inst_mods.size() == 1 && inst_mods[0] == "alu_mod");

		agentlib::tool_context mctx;
		mctx.fs_security.add_allowed_root(tmp_proj.string(), agentlib::access_type::write);
		mctx.fs_security.set_working_directory(tmp_proj.string());

		tools::verilog_configure_testbench_validator cfg_val;
		std::string err;

		// List mode (pure)
		assert(cfg_val.is_pure(nlohmann::json::object()));

		// Configure tb_alu.sv by file path to sv/alu/alu.sv (resolves to alu_mod), level="quick"
		nlohmann::json cfg1 = {
			{"testbench", "tb/tb_alu.sv"},
			{"modules", nlohmann::json::array({"sv/alu/alu.sv"})},
			{"level", "quick"}
		};
		assert(!cfg_val.is_pure(cfg1));
		assert(cfg_val.validate_args(cfg1, mctx, err) && err.empty());
		std::string cfg1_out = cfg_val.create_tool(cfg1)->execute(mctx);
		assert(cfg1_out.find("Configured testbench `tb/tb_alu.sv`") != std::string::npos);
		assert(cfg1_out.find("`alu_mod`") != std::string::npos);
		// Should also warn that tb/tb_alu.sv and tb/tb_cpu_extreme.sv both write to dump.vcd with directive fix!
		assert(cfg1_out.find("Waveform `$dumpfile` Collision Detected") != std::string::npos);
		assert(cfg1_out.find("rename `$dumpfile(\"dump.vcd\")` -> `$dumpfile(\"dump_alu.vcd\")`") != std::string::npos);

		// Configure tb_cpu.sv (level="comprehensive") and tb_cpu_extreme.sv (level="extreme")
		nlohmann::json cfg2 = {
			{"testbench", "tb/tb_cpu.sv"},
			{"module", "cpu"},
			{"level", "comprehensive"}
		};
		assert(cfg_val.validate_args(cfg2, mctx, err) && err.empty());
		cfg_val.create_tool(cfg2)->execute(mctx);

		nlohmann::json cfg3 = {
			{"testbench", "tb/tb_cpu_extreme.sv"},
			{"modules", nlohmann::json::array({"cpu_mod", "alu_mod"})},
			{"level", "extreme"}
		};
		assert(cfg_val.validate_args(cfg3, mctx, err) && err.empty());
		cfg_val.create_tool(cfg3)->execute(mctx);

		// Test removing alu_mod from tb_cpu_extreme.sv while keeping cpu_mod
		nlohmann::json cfg_rem_one = {
			{"testbench", "tb/tb_cpu_extreme.sv"},
			{"module", "alu_mod"},
			{"remove", true}
		};
		assert(cfg_val.validate_args(cfg_rem_one, mctx, err) && err.empty());
		std::string rem_out = cfg_val.create_tool(cfg_rem_one)->execute(mctx);
		assert(rem_out.find("Removed 1 module association(s) from testbench `tb/tb_cpu_extreme.sv`") != std::string::npos);

		if (!fs_utils::find_executable("verilator").empty()) {
			tools::verilog_simulate_validator sim_val;

			// 1) Simulate cpu with hierarchical=false (default) and level="comprehensive" (default):
			//    Should ONLY run tb/tb_cpu.sv (skips tb_alu.sv because hierarchical=false, and skips tb_cpu_extreme.sv because level="comprehensive" < "extreme")
			nlohmann::json sim_cpu_only = {
				{"module", "cpu"}
			};
			assert(sim_val.validate_args(sim_cpu_only, mctx, err) && err.empty());
			std::string out_cpu_only = sim_val.create_tool(sim_cpu_only)->execute(mctx);
			assert(out_cpu_only.find("**1 Passed**, **0 Failed** (out of 1 testbench run(s))") != std::string::npos);
			assert(out_cpu_only.find("`tb/tb_cpu.sv`") != std::string::npos);
			assert(out_cpu_only.find("`tb/tb_alu.sv`") == std::string::npos);
			assert(out_cpu_only.find("`tb/tb_cpu_extreme.sv`") == std::string::npos);
			// Unique dump_cpu.vcd has NO collision, so it keeps its original name dump_cpu.vcd!
			assert(std::filesystem::exists(tmp_proj / "dump_cpu.vcd"));
			assert(out_cpu_only.find("Waveform `$dumpfile` Collision") == std::string::npos);

			// 2) Simulate cpu with hierarchical=true and level="comprehensive":
			//    Should run bottom-up: tb/tb_alu.sv first (for leaf alu_mod), then tb/tb_cpu.sv (for parent cpu_mod)!
			nlohmann::json sim_hier = {
				{"module", "sv/cpu/cpu.sv"},
				{"hierarchical", true},
				{"level", "comprehensive"}
			};
			assert(sim_val.validate_args(sim_hier, mctx, err) && err.empty());
			std::string out_hier = sim_val.create_tool(sim_hier)->execute(mctx);
			assert(out_hier.find("Bottom-Up Module Order**: `alu_mod` -> `cpu_mod`") != std::string::npos);
			assert(out_hier.find("**2 Passed**, **0 Failed** (out of 2 testbench run(s))") != std::string::npos);
			size_t pos_alu = out_hier.find("### Verilog Simulation: `tb/tb_alu.sv`");
			size_t pos_cpu = out_hier.find("### Verilog Simulation: `tb/tb_cpu.sv`");
			assert(pos_alu != std::string::npos && pos_cpu != std::string::npos && pos_alu < pos_cpu);

			// Because tb/tb_alu.sv's "dump.vcd" collides with tb/tb_cpu_extreme.sv's "dump.vcd",
			// its waveform was automatically renamed to tb_alu.vcd and a collision warning was emitted!
			assert(std::filesystem::exists(tmp_proj / "tb_alu.vcd"));
			assert(std::filesystem::exists(tmp_proj / "tb_alu.lastpass.vcd"));
			assert(out_hier.find("Waveform `$dumpfile` Collision") != std::string::npos);
			assert(out_hier.find("Renamed generated waveform to `tb_alu.vcd`") != std::string::npos);
			assert(out_hier.find("rename `$dumpfile(\"dump.vcd\")` -> `$dumpfile(\"dump_alu.vcd\")`") != std::string::npos);
			// And tb_cpu.sv (second run) automatically compared IDENTICAL against its previous run!
			assert(out_hier.find("IDENTICAL to previous") != std::string::npos);

			// 3) Simulate cpu with level="quick" (hierarchical=false):
			//    tb_cpu.sv is "comprehensive" and tb_cpu_extreme.sv is "extreme", so 0 match level="quick" and it reports skipped higher-tier testbenches
			nlohmann::json sim_quick_cpu = {
				{"module", "cpu_mod"},
				{"level", "quick"}
			};
			assert(sim_val.validate_args(sim_quick_cpu, mctx, err) && err.empty());
			std::string out_quick_cpu = sim_val.create_tool(sim_quick_cpu)->execute(mctx);
			assert(out_quick_cpu.find("No testbenches for module `cpu_mod` matched `level=\"quick\"`") != std::string::npos);
			assert(out_quick_cpu.find("Skipped higher-tier testbench(es)") != std::string::npos);

			// 4) Add a probe-only testbench (tb_alu_probe.sv: exits 0 cleanly, logs via $display, no PASS/FAIL assertion, no $dumpfile).
			//    Also intentionally give it the same module name (`module tb_alu;`) as `tb/tb_alu.sv` to verify that Phase 1 parallel
			//    compilation disambiguates `--Mdir` (`verilator_tb_alu_probe_tb_alu` vs `verilator_tb_alu`) without colliding!
			//    Verify it is NOT classified as FAIL, does NOT trigger false $dumpfile collision warnings, and reports COMPLETED (no assertions) + 0 Failed!
			{
				std::ofstream f(tmp_proj / "tb" / "tb_alu_probe.sv");
				f << "`timescale 1ns/1ps\n"
				  << "module tb_alu;\n"
				  << "    logic clk = 0;\n"
				  << "    always #5 clk = ~clk;\n"
				  << "    logic [7:0] a = 8'd5, b = 8'd6, y;\n"
				  << "    alu_mod uut(.clk(clk), .a(a), .b(b), .y(y));\n"
				  << "    initial begin\n"
				  << "        #20;\n"
				  << "        $display(\"probe y=%02h\", y);\n"
				  << "        $finish;\n"
				  << "    end\n"
				  << "endmodule\n";
			}
			assert(verilog::verilog_module_scanner::extract_dumpfile_path(tmp_proj / "tb" / "tb_alu_probe.sv").empty());

			nlohmann::json sim_alu_with_probe = {
				{"module", "alu_mod"}
			};
			assert(sim_val.validate_args(sim_alu_with_probe, mctx, err) && err.empty());
			std::string out_alu_probe = sim_val.create_tool(sim_alu_with_probe)->execute(mctx);
			assert(out_alu_probe.find("**1 Passed**, **1 Info (no assertions)**, **0 Failed**") != std::string::npos);
			assert(out_alu_probe.find("**INFO** (no assertions)") != std::string::npos);
			assert(out_alu_probe.find("**FAIL**") == std::string::npos);

			// 5) Add a top-level wrapper module soc_top (instantiating cpu_mod) with NO direct testbench.
			//    Verify verilog_simulate(module="soc_top") (default hierarchical=false) detects submodule testbenches
			//    and recommends running with hierarchical=true!
			{
				std::ofstream f(tmp_proj / "sv" / "soc_top.sv");
				f << "module soc_top(input logic clk, input logic [7:0] a, b, output logic [7:0] y);\n"
				  << "    cpu_mod cpu_inst(.clk(clk), .in_a(a), .in_b(b), .out_y(y));\n"
				  << "endmodule\n";
			}
			nlohmann::json sim_soc_nonhier = {
				{"module", "soc_top"}
			};
			assert(sim_val.validate_args(sim_soc_nonhier, mctx, err) && err.empty());
			std::string out_soc_nonhier = sim_val.create_tool(sim_soc_nonhier)->execute(mctx);
			assert(out_soc_nonhier.find("No direct testbench is associated with module `soc_top`") != std::string::npos);
			assert(out_soc_nonhier.find("verilog_configure_testbench(module=\"soc_top\", hierarchical=true)") != std::string::npos);
			assert(out_soc_nonhier.find("verilog_simulate(module=\"soc_top\", hierarchical=true)") != std::string::npos);
			assert(out_soc_nonhier.find("`alu_mod`") != std::string::npos);
			assert(out_soc_nonhier.find("`cpu_mod`") != std::string::npos);

			// 6) Configure default hierarchical=true for soc_top in turbostar.json via verilog_configure_testbench,
			//    then verify verilog_simulate(module="soc_top") runs hierarchically without needing hierarchical=true,
			//    while explicit hierarchical=false still overrides it.
			nlohmann::json cfg_soc_hier = {
				{"module", "soc_top"},
				{"hierarchical", true}
			};
			assert(cfg_val.validate_args(cfg_soc_hier, mctx, err) && err.empty());
			std::string out_cfg_soc_hier = cfg_val.create_tool(cfg_soc_hier)->execute(mctx);
			assert(out_cfg_soc_hier.find("Set default `hierarchical=true` for module(s) `soc_top`.") != std::string::npos);
			assert(out_cfg_soc_hier.find("**Modules with Default `hierarchical=true`**: `soc_top`") != std::string::npos);

			std::string out_soc_default_hier = sim_val.create_tool(sim_soc_nonhier)->execute(mctx);
			assert(out_soc_default_hier.find("Ran with `hierarchical=true` due to project settings for `soc_top` in `turbostar.json`") != std::string::npos);
			assert(out_soc_default_hier.find("`alu_mod` -> `cpu_mod` -> `soc_top`") != std::string::npos);

			// Run with level="comprehensive" so 3 testbenches run (> 2):
			// clean passing tb_cpu.sv detail section is omitted by default, while probe-only tb_alu_probe.sv is still shown
			nlohmann::json sim_soc_comp = {
				{"module", "soc_top"},
				{"level", "comprehensive"}
			};
			assert(sim_val.validate_args(sim_soc_comp, mctx, err) && err.empty());
			std::string out_soc_comp = sim_val.create_tool(sim_soc_comp)->execute(mctx);
			assert(out_soc_comp.find("### Verilog Simulation: `tb/tb_cpu.sv`") == std::string::npos);
			assert(out_soc_comp.find("Omitted detailed simulation logs for") != std::string::npos);
			assert(out_soc_comp.find("### Verilog Simulation: `tb/tb_alu_probe.sv`") != std::string::npos);

			// With verbose=true, all per-testbench sections (including clean passing tb_cpu.sv) are shown
			nlohmann::json sim_soc_verbose = {
				{"module", "soc_top"},
				{"level", "comprehensive"},
				{"verbose", true},
				{"timeout_ms", 15000}
			};
			assert(sim_val.validate_args(sim_soc_verbose, mctx, err) && err.empty());
			std::string out_soc_verbose = sim_val.create_tool(sim_soc_verbose)->execute(mctx);
			assert(out_soc_verbose.find("### Verilog Simulation: `tb/tb_cpu.sv`") != std::string::npos);

			// With summary_only=true (or quiet=true), even a single-testbench run omits passing detail sections
			nlohmann::json sim_cpu_quiet = {
				{"module", "cpu_mod"},
				{"level", "comprehensive"},
				{"quiet", true}
			};
			assert(sim_val.validate_args(sim_cpu_quiet, mctx, err) && err.empty());
			std::string out_cpu_quiet = sim_val.create_tool(sim_cpu_quiet)->execute(mctx);
			assert(out_cpu_quiet.find("## Module Verification Report: `cpu_mod`") != std::string::npos);
			assert(out_cpu_quiet.find("### Verilog Simulation: `tb/tb_cpu.sv`") == std::string::npos);

			nlohmann::json sim_soc_explicit_false = {
				{"module", "soc_top"},
				{"hierarchical", false}
			};
			assert(sim_val.validate_args(sim_soc_explicit_false, mctx, err) && err.empty());
			std::string out_soc_override_false = sim_val.create_tool(sim_soc_explicit_false)->execute(mctx);
			assert(out_soc_override_false.find("No direct testbench is associated with module `soc_top`") != std::string::npos);
		}

		// 7) Test scan=true mode, string-literal scrubbing, and dut/uut preference over helper modules
		{
			{
				std::ofstream f(tmp_proj / "sv" / "helper_ram.sv");
				f << "module helper_ram(input logic clk);\n"
				  << "endmodule\n";
			}
			{
				std::ofstream f(tmp_proj / "tb" / "tb_cpu_with_ram.sv");
				f << "`timescale 1ns/1ps\n"
				  << "module tb_cpu_with_ram;\n"
				  << "    logic clk = 0;\n"
				  << "    logic [7:0] a = 8'd1, b = 8'd2, y;\n"
				  << "    cpu_mod dut(.clk(clk), .in_a(a), .in_b(b), .out_y(y));\n"
				  << "    helper_ram mem(.clk(clk));\n"
				  << "    initial begin\n"
				  << "        $display(\"PASS: alu_mod pairs (1234)\");\n"
				  << "        $finish;\n"
				  << "    end\n"
				  << "endmodule\n";
			}
			auto updated_rtl = verilog::verilog_module_scanner::find_synthesizable_files(tmp_proj / "sv");
			auto dut_mods = verilog::verilog_module_scanner::discover_instantiated_modules(
				tmp_proj / "tb" / "tb_cpu_with_ram.sv", updated_rtl);
			// Should prefer 'dut' (cpu_mod) over 'mem' (helper_ram), and ignore 'alu_mod pairs (' inside $display string literal
			assert(dut_mods.size() == 1 && dut_mods[0] == "cpu_mod");

			// Clear all existing testbenches & hierarchical_modules, then run scan=true
			nlohmann::json cfg_clear_all = {{"remove", true}};
			assert(cfg_val.validate_args(cfg_clear_all, mctx, err) && err.empty());
			cfg_val.create_tool(cfg_clear_all)->execute(mctx);

			nlohmann::json cfg_scan = {{"scan", true}};
			assert(!cfg_val.is_pure(cfg_scan));
			assert(cfg_val.validate_args(cfg_scan, mctx, err) && err.empty());
			std::string scan_out = cfg_val.create_tool(cfg_scan)->execute(mctx);
			assert(scan_out.find("Scanned ") != std::string::npos);
			assert(scan_out.find("enabled default `hierarchical=true` on 2 composite module(s)") != std::string::npos);
			assert(scan_out.find("`cpu_mod`") != std::string::npos);
			assert(scan_out.find("`soc_top`") != std::string::npos);

			std::vector<verilog::testbench_entry> scanned_entries;
			std::string used_cfg, load_err;
			assert(verilog::fpga_project_settings::load_testbenches(tmp_proj.string(), scanned_entries, used_cfg, load_err));
			assert(scanned_entries.size() >= 4);

			std::vector<std::string> scanned_hier;
			assert(verilog::fpga_project_settings::load_hierarchical_modules(tmp_proj.string(), scanned_hier, load_err));
			assert(std::find(scanned_hier.begin(), scanned_hier.end(), "cpu_mod") != scanned_hier.end());
			assert(std::find(scanned_hier.begin(), scanned_hier.end(), "soc_top") != scanned_hier.end());
			assert(std::find(scanned_hier.begin(), scanned_hier.end(), "alu_mod") == scanned_hier.end());
		}

		std::filesystem::remove_all(tmp_proj);
	}

	// Test directory-flexible support for wave/ + src/ layout in vcd_label_manager and vcd_parser_cache
	{
		std::filesystem::path wave_proj = std::filesystem::temp_directory_path() / "turbostar_vcd_wave_src_test";
		std::filesystem::remove_all(wave_proj);
		std::filesystem::create_directories(wave_proj / "src");
		std::filesystem::create_directories(wave_proj / "wave");

		std::filesystem::path wave_vcd = wave_proj / "wave" / "dump_main.vcd";
		std::filesystem::copy_file(vcd_path, wave_vcd);

		// 1) Global sidecar path for wave/dump_main.vcd should resolve to project root (wave_proj/vcd_labels.json)
		std::string g_sidecar = verilog::vcd_label_manager::get_global_sidecar_path(wave_vcd.string());
		assert(std::filesystem::path(g_sidecar).parent_path() == std::filesystem::weakly_canonical(wave_proj));

		// 2) Stale RTL warning should detect newer RTL file inside sibling src/ directory
		std::filesystem::path rtl_src = wave_proj / "src" / "main.sv";
		{
			std::ofstream ofs(rtl_src);
			ofs << "module main(input logic clk); endmodule\n";
		}
		auto future_time = std::filesystem::last_write_time(wave_vcd) + std::chrono::seconds(10);
		std::filesystem::last_write_time(rtl_src, future_time);

		std::string stale_warn = verilog::vcd_parser_cache::check_stale_rtl_warning(wave_vcd.string(), "wave/dump_main.vcd");
		assert(stale_warn.find("Stale VCD") != std::string::npos);
		assert(stale_warn.find("main.sv") != std::string::npos);

		// 3) Verify vcd_wave radix="dec" and d:signal prefix formatting
		{
			tools::vcd_wave_validator wave_val;
			agentlib::tool_context wctx;
			wctx.fs_security.add_allowed_root("tests/fixtures", agentlib::access_type::read);
			wctx.fs_security.set_working_directory(".");
			std::string werr;

			nlohmann::json wave_dec_args = {
				{"path", vcd_path},
				{"signals", nlohmann::json::array({"current_ip", "x:current_ip", "b:current_ip"})},
				{"radix", "dec"},
				{"start_tick", 0},
				{"end_tick", 10}
			};
			assert(wave_val.validate_args(wave_dec_args, wctx, werr) && werr.empty());
			std::string wave_dec_out = wave_val.create_tool(wave_dec_args)->execute(wctx);
			assert(wave_dec_out.find("| 0 | 0x0000 ") != std::string::npos);

			nlohmann::json wave_prefix_args = {
				{"path", vcd_path},
				{"signals", nlohmann::json::array({"d:current_ip"})},
				{"start_tick", 0},
				{"end_tick", 10}
			};
			assert(wave_val.validate_args(wave_prefix_args, wctx, werr) && werr.empty());
			std::string wave_prefix_out = wave_val.create_tool(wave_prefix_args)->execute(wctx);
			assert(wave_prefix_out.find("| 0 |") != std::string::npos);
		}

		std::filesystem::remove_all(wave_proj);
	}

	// Test vcd_wave 'instance' parameter (instance name, RTL module name, and multi-instance first-match)
	{
		std::filesystem::path inst_proj = std::filesystem::temp_directory_path() / "turbostar_vcd_wave_instance_test";
		std::filesystem::remove_all(inst_proj);
		std::filesystem::create_directories(inst_proj / "rtl");
		std::filesystem::create_directories(inst_proj / "tb");

		{
			std::ofstream ofs(inst_proj / "rtl" / "alu_mod.sv");
			ofs << "module alu_mod(\n"
			       "    input logic clk,\n"
			       "    input logic rst,\n"
			       "    input logic [7:0] a, b,\n"
			       "    output logic [7:0] y\n"
			       ");\n"
			       "    assign y = rst ? 8'h00 : (a + b);\n"
			       "endmodule\n";
		}
		{
			std::ofstream ofs(inst_proj / "rtl" / "reg_cell.sv");
			ofs << "module reg_cell(\n"
			       "    input logic clk,\n"
			       "    input logic rst,\n"
			       "    input logic [7:0] d,\n"
			       "    output logic [7:0] q\n"
			       ");\n"
			       "endmodule\n";
		}
		{
			std::ofstream ofs(inst_proj / "rtl" / "top_mod.sv");
			ofs << "module top_mod(input logic clk, input logic rst);\n"
			       "    alu_mod alu(.clk(clk), .rst(rst), .a(8'h10), .b(8'h20), .y());\n"
			       "    reg_cell reg_a(.clk(clk), .rst(rst), .d(8'h11), .q());\n"
			       "    reg_cell reg_b(.clk(clk), .rst(rst), .d(8'h22), .q());\n"
			       "endmodule\n";
		}
		{
			std::ofstream ofs(inst_proj / "tb" / "tb_top.sv");
			ofs << "module tb_top;\n"
			       "    logic clk;\n"
			       "    top_mod uut(.clk(clk), .rst(1'b0));\n"
			       "    initial begin $dumpfile(\"dump.vcd\"); $dumpvars(0, tb_top); end\n"
			       "endmodule\n";
		}
		{
			std::ofstream ofs(inst_proj / "dump.vcd");
			ofs << "$timescale 1ns $end\n"
			       "$scope module tb_top $end\n"
			       "$var wire 1 ! clk $end\n"
			       "$scope module uut $end\n"
			       "$scope module alu $end\n"
			       "$var wire 1 ! clk $end\n"
			       "$var wire 1 \" rst $end\n"
			       "$var wire 8 # a [7:0] $end\n"
			       "$var wire 8 $ b [7:0] $end\n"
			       "$var wire 8 % y [7:0] $end\n"
			       "$upscope $end\n"
			       "$scope module reg_a $end\n"
			       "$var wire 1 ! clk $end\n"
			       "$var wire 1 \" rst $end\n"
			       "$var wire 8 & d [7:0] $end\n"
			       "$var wire 8 ' q [7:0] $end\n"
			       "$upscope $end\n"
			       "$scope module reg_b $end\n"
			       "$var wire 1 ! clk $end\n"
			       "$var wire 1 \" rst $end\n"
			       "$var wire 8 ( d [7:0] $end\n"
			       "$var wire 8 ) q [7:0] $end\n"
			       "$upscope $end\n"
			       "$upscope $end\n"
			       "$upscope $end\n"
			       "$enddefinitions $end\n"
			       "#0\n0!\n1\"\nb00010000 #\nb00100000 $\nb00000000 %\nb00010001 &\nb00000000 '\nb00100010 (\nb00000000 )\n"
			       "#5\n1!\n"
			       "#10\n0!\n0\"\nb00110000 %\nb00010001 '\nb00100010 )\n"
			       "#15\n1!\n"
			       "#20\n0!\n";
		}

		tools::vcd_wave_validator wave_val;
		agentlib::tool_context ictx;
		ictx.fs_security.add_allowed_root(inst_proj.string(), agentlib::access_type::read);
		ictx.fs_security.set_working_directory(inst_proj.string());

		// 1) Instance by name ("alu") with auto-detected VCD path
		{
			std::string ierr;
			nlohmann::json args_inst = {
				{"instance", "alu"}
			};
			assert(wave_val.validate_args(args_inst, ictx, ierr) && ierr.empty());
			std::string out_inst = wave_val.create_tool(args_inst)->execute(ictx);
			assert(out_inst.find("Instance: `tb_top.uut.alu` (module `alu_mod`)") != std::string::npos);
			assert(out_inst.find("Inputs: `rst`, `a`, `b` | Outputs: `y`") != std::string::npos);
			assert(out_inst.find("| `rst` | `a` | `b` | `y` |") != std::string::npos);
			assert(out_inst.find("0x30") != std::string::npos);
		}

		// 2) By RTL module name via 'module' alias ("alu_mod")
		{
			std::string ierr;
			nlohmann::json args_mod = {
				{"module", "alu_mod"}
			};
			assert(wave_val.validate_args(args_mod, ictx, ierr) && ierr.empty());
			std::string out_mod = wave_val.create_tool(args_mod)->execute(ictx);
			assert(out_mod.find("Instance: `tb_top.uut.alu` (module `alu_mod`)") != std::string::npos);
		}

		// 3) Multi-instance module ("reg_cell") picks first instance (reg_a) and notes reg_b
		{
			std::string ierr;
			nlohmann::json args_multi = {
				{"instance", "reg_cell"}
			};
			assert(wave_val.validate_args(args_multi, ictx, ierr) && ierr.empty());
			std::string out_multi = wave_val.create_tool(args_multi)->execute(ictx);
			assert(out_multi.find("Instance: `tb_top.uut.reg_a` (module `reg_cell`)") != std::string::npos);
			assert(out_multi.find("Other instances: `tb_top.uut.reg_b`") != std::string::npos);
			assert(out_multi.find("0x11") != std::string::npos);
		}

		// 4) Nonexistent instance reports available scopes
		{
			std::string ierr;
			nlohmann::json args_bad = {
				{"instance", "does_not_exist"}
			};
			assert(wave_val.validate_args(args_bad, ictx, ierr) && ierr.empty());
			std::string out_bad = wave_val.create_tool(args_bad)->execute(ictx);
			assert(out_bad.find("not found in VCD") != std::string::npos);
			assert(out_bad.find("tb_top.uut.alu") != std::string::npos);
		}

		std::filesystem::remove_all(inst_proj);
	}

	std::filesystem::remove(vcd_path + ".labels.json");
	std::filesystem::remove("tests/fixtures/test_dump_org.vcd.labels.json");
	std::filesystem::remove("tests/fixtures/test_dump_org.vcd.qualifiers.json");
	std::filesystem::remove("tests/fixtures/test_dump_mutated.vcd.qualifiers.json");
	std::filesystem::remove("tests/fixtures/vcd_labels.json");
	std::filesystem::remove("vcd_labels.json");

	std::cout << "All VCD unit tests passed!\n";
	return 0;
}
