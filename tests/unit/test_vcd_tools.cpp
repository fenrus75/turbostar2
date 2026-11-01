// Tested source file: src/plugins/verilog/vcd_parser.cpp
#include "test_watchdog.h"
#include "plugins/verilog/vcd_parser.h"
#include "plugins/verilog/vcd_file_info_tool.h"
#include "plugins/verilog/vcd_file_info_validator.h"
#include "plugins/verilog/vcd_wave_tool.h"
#include "plugins/verilog/vcd_wave_validator.h"
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
			{"signals", nlohmann::json::array({"tb_main.uut.cpu8008.opcode", "x:tb_main.uut.cpu8008.current_ip"})},
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

	std::cout << "All VCD unit tests passed!\n";
	return 0;
}
