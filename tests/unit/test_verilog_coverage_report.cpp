// Tested source file: src/plugins/verilog/verilog_coverage_report.cpp
#include "plugins/verilog/verilog_coverage_report.h"
#include "test_watchdog.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>

namespace fs = std::filesystem;

int main()
{
	test_watchdog::setup_watchdog(30);

	fs::path tmp_root = fs::temp_directory_path() / ("test_verilog_cov_" + std::to_string(getpid()));
	fs::remove_all(tmp_root);
	fs::create_directories(tmp_root / "src");

	fs::path rtl_file = tmp_root / "src" / "mini_alu.sv";
	{
		std::ofstream out(rtl_file);
		out << "module mini_alu(\n"
		    << "    input logic clk,\n"
		    << "    input logic carry_in,\n"
		    << "    input logic unused_in,\n"
		    << "    output logic [7:0] y\n"
		    << ");\n"
		    << "    always_comb begin\n"
		    << "        if (carry_in) y = 8'h01;\n"
		    << "        else y = 8'h00;\n"
		    << "    end\n"
		    << "endmodule\n";
	}
	std::vector<std::string> rtl_files = {rtl_file.string()};

	// Test 1: Verilator 5.032 format (no 't' field; type encoded in 'page'; bare toggle 'o' without :0->1/:1->0)
	{
		fs::path dat_5032 = tmp_root / "cov_5032.dat";
		{
			std::ofstream out(dat_5032, std::ios::binary);
			out << "# SystemC::Coverage-3\n";
			out << "C '\x01" "f\x02" "src/mini_alu.sv\x01" "l\x02" "8\x01" "n\x02" "9\x01" "page\x02" "v_line/mini_alu\x01" "o\x02" "block\x01" "S\x02" "8\x01" "h\x02" "tb.dut' 10\n";
			out << "C '\x01" "f\x02" "src/mini_alu.sv\x01" "l\x02" "9\x01" "n\x02" "9\x01" "page\x02" "v_line/mini_alu\x01" "o\x02" "block\x01" "S\x02" "9\x01" "h\x02" "tb.dut' 0\n";
			out << "C '\x01" "f\x02" "src/mini_alu.sv\x01" "l\x02" "3\x01" "n\x02" "1\x01" "page\x02" "v_toggle/mini_alu\x01" "o\x02" "carry_in\x01" "h\x02" "tb.dut' 4\n";
			out << "C '\x01" "f\x02" "src/mini_alu.sv\x01" "l\x02" "4\x01" "n\x02" "1\x01" "page\x02" "v_toggle/mini_alu\x01" "o\x02" "unused_in\x01" "h\x02" "tb.dut' 0\n";
			// Testbench file entry must be filtered out
			out << "C '\x01" "f\x02" "tb/mini_alu_tb.sv\x01" "l\x02" "5\x01" "n\x02" "1\x01" "page\x02" "v_line/mini_alu_tb\x01" "o\x02" "block\x01" "S\x02" "5\x01" "h\x02" "tb' 0\n";
		}

		std::vector<fs::path> dats = {dat_5032};
		auto stats = verilog::verilog_coverage_report::parse_and_merge(tmp_root, rtl_files, dats);
		assert(stats.size() == 1);
		assert(stats[0].module_name == "mini_alu");
		assert(stats[0].total_lines == 2);
		assert(stats[0].covered_lines == 1);
		assert(stats[0].uncovered_ranges.size() == 1);
		assert(stats[0].uncovered_ranges[0].start_line == 9);
		assert(stats[0].untoggled_ports.size() == 1);
		assert(stats[0].untoggled_ports[0].port_name == "unused_in");
	}

	// Test 2: Verilator 5.048 format (explicit 't' field and :0->1 / :1->0 toggle suffixes)
	{
		fs::path dat_5048 = tmp_root / "cov_5048.dat";
		{
			std::ofstream out(dat_5048, std::ios::binary);
			out << "# SystemC::Coverage-3\n";
			out << "C '\x01" "f\x02" "src/mini_alu.sv\x01" "l\x02" "8\x01" "n\x02" "9\x01" "t\x02" "line\x01" "page\x02" "v_line/mini_alu\x01" "o\x02" "block\x01" "S\x02" "8-9\x01" "h\x02" "tb.dut' 5\n";
			out << "C '\x01" "f\x02" "src/mini_alu.sv\x01" "l\x02" "3\x01" "n\x02" "1\x01" "t\x02" "toggle\x01" "page\x02" "v_toggle/mini_alu\x01" "o\x02" "carry_in:0->1\x01" "h\x02" "tb.dut' 2\n";
			out << "C '\x01" "f\x02" "src/mini_alu.sv\x01" "l\x02" "3\x01" "n\x02" "1\x01" "t\x02" "toggle\x01" "page\x02" "v_toggle/mini_alu\x01" "o\x02" "carry_in:1->0\x01" "h\x02" "tb.dut' 0\n";
		}

		std::vector<fs::path> dats = {dat_5048};
		auto stats = verilog::verilog_coverage_report::parse_and_merge(tmp_root, rtl_files, dats);
		assert(stats.size() == 1);
		assert(stats[0].total_lines == 2);
		assert(stats[0].covered_lines == 2);
		assert(stats[0].untoggled_ports.size() == 1);
		assert(stats[0].untoggled_ports[0].port_name == "carry_in");
		assert(!stats[0].untoggled_ports[0].never_rose);
		assert(stats[0].untoggled_ports[0].never_fell);
	}

	// Test 3: 0/0 instrumented lines must report N/A (not 100.0% / "All instrumented lines covered")
	{
		fs::path dat_empty_lines = tmp_root / "cov_toggle_only.dat";
		{
			std::ofstream out(dat_empty_lines, std::ios::binary);
			out << "# SystemC::Coverage-3\n";
			out << "C '\x01" "f\x02" "src/mini_alu.sv\x01" "l\x02" "3\x01" "n\x02" "1\x01" "page\x02" "v_toggle/mini_alu\x01" "o\x02" "carry_in\x01" "h\x02" "tb.dut' 2\n";
		}
		std::vector<fs::path> dats = {dat_empty_lines};
		auto stats = verilog::verilog_coverage_report::parse_and_merge(tmp_root, rtl_files, dats);
		assert(stats.size() == 1);
		assert(stats[0].total_lines == 0);
		std::string summary = verilog::verilog_coverage_report::format_summary_markdown(stats, 1, false);
		assert(summary.find("N/A (0/0)") != std::string::npos);
		assert(summary.find("100.0%") == std::string::npos);
		std::string detail = verilog::verilog_coverage_report::format_detailed_markdown(stats, 1);
		assert(detail.find("All instrumented lines covered") == std::string::npos);
	}

	// Test 4: Delta state preserves per-module history across interleaved runs (alu8 -> decode_mod -> alu8)
	{
		fs::path state_path = tmp_root / "verilog_coverage_last.json";

		verilog::module_coverage_stats alu_run1;
		alu_run1.module_name = "alu8";
		alu_run1.total_lines = 100;
		alu_run1.covered_lines = 60;
		alu_run1.coverage_pct = 60.0;
		std::vector<verilog::module_coverage_stats> s1 = {alu_run1};
		verilog::verilog_coverage_report::apply_and_save_deltas(state_path, s1);
		assert(!s1[0].has_prev_pct);

		verilog::module_coverage_stats dec_run;
		dec_run.module_name = "decode_mod";
		dec_run.total_lines = 100;
		dec_run.covered_lines = 50;
		dec_run.coverage_pct = 50.0;
		std::vector<verilog::module_coverage_stats> s2 = {dec_run};
		verilog::verilog_coverage_report::apply_and_save_deltas(state_path, s2);
		assert(!s2[0].has_prev_pct);

		verilog::module_coverage_stats alu_run2 = alu_run1;
		alu_run2.covered_lines = 95;
		alu_run2.coverage_pct = 95.0;
		std::vector<verilog::module_coverage_stats> s3 = {alu_run2};
		verilog::verilog_coverage_report::apply_and_save_deltas(state_path, s3);
		assert(s3[0].has_prev_pct);
		assert(s3[0].prev_pct == 60.0);
	}

	fs::remove_all(tmp_root);
	std::cout << "test_verilog_coverage_report passed!\n";
	return 0;
}
