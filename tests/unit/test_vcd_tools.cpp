// Tested source file: src/plugins/verilog/vcd_parser.cpp
#include "test_watchdog.h"
#include "plugins/verilog/vcd_parser.h"
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
#include "agentlib/tool_context.h"
#include <cassert>
#include <iostream>

int main()
{
	test_watchdog::setup_watchdog(30);

	const std::string vcd_path = "tests/fixtures/test_dump.vcd";

	// 1. Direct vcd_parser tests
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
	}

	// Clean up any test sidecar created during tests
	std::filesystem::remove(vcd_path + ".labels.json");
	std::filesystem::remove("tests/fixtures/test_dump_org.vcd.labels.json");
	std::filesystem::remove("tests/fixtures/test_dump_org.vcd.qualifiers.json");
	std::filesystem::remove("tests/fixtures/test_dump_mutated.vcd.labels.json");
	std::filesystem::remove("tests/fixtures/test_dump_mutated.vcd.qualifiers.json");

	std::cout << "All VCD unit tests passed!\n";
	return 0;
}
