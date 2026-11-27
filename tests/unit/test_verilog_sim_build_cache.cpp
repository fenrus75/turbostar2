// Tested source file: src/plugins/verilog/verilog_sim_build_cache.cpp, src/project_manager.cpp
#include "plugins/verilog/fpga_project_settings.h"
#include "plugins/verilog/verilog_sim_build_cache.h"
#include "project_manager.h"
#include "test_watchdog.h"

#include <atomic>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>

namespace fs = std::filesystem;

static void write_text(const fs::path &p, const std::string &s)
{
	fs::create_directories(p.parent_path());
	std::ofstream out(p, std::ios::binary | std::ios::trunc);
	out << s;
}

static void test_signature_and_cache()
{
	fs::path tmp = fs::temp_directory_path() / ("test_sim_cache_" + std::to_string(getpid()));
	fs::remove_all(tmp);
	fs::create_directories(tmp / "src");
	fs::create_directories(tmp / "tb");

	fs::path alu = tmp / "src" / "alu.sv";
	fs::path cpu = tmp / "src" / "cpu.sv";
	fs::path tb = tmp / "tb" / "tb_alu.sv";
	write_text(alu, "module alu(input logic a, output logic b); assign b = a; endmodule\n");
	write_text(cpu, "module cpu(input logic a, output logic b); alu u0(.a(a), .b(b)); endmodule\n");
	write_text(tb, "module tb_alu; logic a, b; alu dut(.a(a), .b(b)); endmodule\n");

	std::vector<std::string> rtl = {alu.string(), cpu.string()};
	auto scoped = verilog::verilog_sim_build_cache::collect_tb_sources(tb.string(), rtl, {});
	// tb_alu only instantiates alu, so cpu.sv must NOT be included
	bool has_alu = false;
	bool has_cpu = false;
	for (const auto &s : scoped) {
		if (s.find("alu.sv") != std::string::npos) has_alu = true;
		if (s.find("cpu.sv") != std::string::npos) has_cpu = true;
	}
	assert(has_alu);
	assert(!has_cpu);

	std::string sig1 = verilog::verilog_sim_build_cache::compute_build_signature(scoped, "tb_alu", false, false);
	std::string sig_cov = verilog::verilog_sim_build_cache::compute_build_signature(scoped, "tb_alu", true, false);
	assert(!sig1.empty());
	assert(sig1 != sig_cov);

	fs::path mdir = tmp / "mdir";
	fs::path bin = mdir / "Vtb_alu";
	write_text(bin, "fake-elf");
	assert(!verilog::verilog_sim_build_cache::is_build_up_to_date(mdir, bin, sig1));

	verilog::verilog_sim_build_cache::write_build_signature(mdir, sig1);
	assert(verilog::verilog_sim_build_cache::is_build_up_to_date(mdir, bin, sig1));
	assert(!verilog::verilog_sim_build_cache::is_build_up_to_date(mdir, bin, sig_cov));

	// Editing alu.sv changes size/mtime -> signature invalidates
	write_text(alu, "module alu(input logic a, output logic b); assign b = ~a; /* edited */ endmodule\n");
	std::string sig2 = verilog::verilog_sim_build_cache::compute_build_signature(scoped, "tb_alu", false, false);
	assert(sig2 != sig1);
	assert(!verilog::verilog_sim_build_cache::is_build_up_to_date(mdir, bin, sig2));

	fs::remove_all(tmp);
}

static void test_aot_session_gate_and_listener()
{
	auto &cache = verilog::verilog_sim_build_cache::get_instance();
	cache.reset_session_for_testing();
	assert(!cache.is_aot_session_active());

	// CLI mode must never activate AOT background pre-compilation
	verilog::fpga_project_settings::set_cli_mode(true);
	cache.record_simulation_session("/tmp/fake_proj", false, true);
	assert(!cache.is_aot_session_active());

	// Interactive/MCP mode activates AOT after first simulate call
	verilog::fpga_project_settings::set_cli_mode(false);
	cache.record_simulation_session("/tmp/fake_proj", false, true);
	assert(cache.is_aot_session_active());

	// project_manager file-change listener registration & fan-out
	std::atomic<int> hits{0};
	uint64_t lid = project_manager::get_instance().register_file_change_listener([&](const std::string &p) {
		if (p.find("alu.sv") != std::string::npos) {
			hits.fetch_add(1);
		}
	});
	project_manager::get_instance().notify_file_changed("/tmp/fake_proj/src/alu.sv");
	assert(hits.load() == 1);
	project_manager::get_instance().unregister_file_change_listener(lid);
	project_manager::get_instance().notify_file_changed("/tmp/fake_proj/src/alu.sv");
	assert(hits.load() == 1);

	cache.shutdown();
	assert(!cache.is_aot_session_active());
}

int main()
{
	test_watchdog::setup_watchdog(30);

	test_signature_and_cache();
	test_aot_session_gate_and_listener();

	std::cout << "PASS: test_verilog_sim_build_cache\n";
	return 0;
}
