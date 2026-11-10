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
}
int main()
{
	test_watchdog::setup_watchdog(30);

	const std::string vcd_path = "tests/fixtures/test_dump.vcd";
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
			{"qualifier", "tb_main.uut.cpu8008.decode.mem_req_en"}
		};
		bool q_ok = q_val.validate_args(q_args_a, ctx, err);
		assert(q_ok && err.empty());
		auto q_tool_a = q_val.create_tool(q_args_a);
		q_tool_a->execute(ctx);

		nlohmann::json q_args_b = {
			{"path", mut_vcd},
			{"signal", "tb_main.uut.cpu8008.decode.mem_data"},
			{"qualifier", "tb_main.uut.cpu8008.decode.mem_req_en"}
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
			{"clear", true}
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

		// Mutate DUT so the next simulation run produces a failing result, and verify both
		// sim_out.previous.vcd and sim_out.lastpass.vcd are preserved and suggested for vcd_compare
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
		assert(sim_out_2.find("Previous result saved as `sim_out.previous.vcd`. To compare the current result to this file use `vcd_compare(path_a=\"sim_out.previous.vcd\", path_b=\"sim_out.vcd\", scope=\"tb_dut.uut\")`") != std::string::npos);
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

	// Clean up any test sidecars created during tests
	std::filesystem::remove(vcd_path + ".labels.json");
	std::filesystem::remove("tests/fixtures/test_dump_org.vcd.labels.json");
	std::filesystem::remove("tests/fixtures/test_dump_org.vcd.qualifiers.json");
	std::filesystem::remove("tests/fixtures/test_dump_mutated.vcd.labels.json");
	std::filesystem::remove("tests/fixtures/test_dump_mutated.vcd.qualifiers.json");

	std::cout << "All VCD unit tests passed!\n";
	return 0;
}
