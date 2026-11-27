// Tested source file: src/plugins/verilog/verilog_formal_tool.cpp, src/plugins/verilog/verilog_formal_validator.cpp
#include "agentlib/tool_context.h"
#include "fs_utils.h"
#include "plugins/verilog/fpga_project_settings.h"
#include "plugins/verilog/verilog_formal_tool.h"
#include "plugins/verilog/verilog_formal_validator.h"
#include "plugins/verilog/verilog_project_settings_validator.h"
#include "test_watchdog.h"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

static void test_validator()
{
	tools::verilog_formal_validator v;
	agentlib::tool_context ctx;
	std::string err;

	// Default empty args should be valid (mode=all, depth=20, solver=auto, timeout=120)
	assert(v.validate_args(nlohmann::json::object(), ctx, err));

	// Explicit valid args including prove+cover, parameter overrides, and verbose
	nlohmann::json good = {
		{"module", "alu8"},
		{"mode", "prove+cover"},
		{"depth", 15},
		{"solver", "bitwuzla"},
		{"timeout_sec", 60},
		{"parameters", {{"COUNTER_BITS", 4}, {"WIDTH", "8'd16"}}},
		{"verbose", true},
	};
	assert(v.validate_args(good, ctx, err));
	assert(v.validate_args(nlohmann::json{{"mode", "bmc+cover"}}, ctx, err));

	// Invalid parameter name / shell injection rejected
	assert(!v.validate_args(nlohmann::json{{"parameters", {{"BAD;NAME", "4"}}}}, ctx, err));
	assert(!v.validate_args(nlohmann::json{{"parameters", {{"COUNTER_BITS", "4; rm -rf /"}}}}, ctx, err));

	// Invalid mode
	nlohmann::json bad_mode = {{"mode", "bogus"}};
	assert(!v.validate_args(bad_mode, ctx, err));
	assert(err.find("mode") != std::string::npos);

	// Invalid solver
	nlohmann::json bad_solver = {{"solver", "unknown_solver"}};
	assert(!v.validate_args(bad_solver, ctx, err));
	assert(err.find("solver") != std::string::npos);

	// Out-of-range depth and timeout_sec are clamped cleanly; timeout_seconds alias accepted
	nlohmann::json clamped = {{"depth", 0}, {"timeout_sec", 99999}};
	assert(v.validate_args(clamped, ctx, err));
	assert(v.validate_args(nlohmann::json{{"timeout_seconds", 90}}, ctx, err));

	// Control characters in module name are rejected
	nlohmann::json bad_mod = {{"module", "alu8\x1b[31m"}};
	assert(!v.validate_args(bad_mod, ctx, err));
}

static void test_find_executable_tilde_expansion()
{
	const char *home = std::getenv("HOME");
	assert(home && *home);

	fs::path bin_dir = fs::path(home) / "custom_sby_test_bin";
	fs::create_directories(bin_dir);
	fs::path fake_exe = bin_dir / "fake_sby_probe";
	{
		std::ofstream out(fake_exe);
		out << "#!/bin/sh\nexit 0\n";
	}
	::chmod(fake_exe.c_str(), 0755);

	// Direct ~/ path expansion
	std::string direct = fs_utils::find_executable("~/custom_sby_test_bin/fake_sby_probe");
	assert(direct == fake_exe.string());

	// PATH entry with leading ~/
	const char *orig_path = std::getenv("PATH");
	std::string saved_path = orig_path ? orig_path : "";
	std::string new_path = "~/custom_sby_test_bin:" + saved_path;
	::setenv("PATH", new_path.c_str(), 1);

	std::string via_path = fs_utils::find_executable("fake_sby_probe");
	assert(via_path == fake_exe.string());

	::setenv("PATH", saved_path.c_str(), 1);
	fs::remove_all(bin_dir);
}

static void test_sby_path_settings_roundtrip()
{
	fs::path tmp_proj = fs::temp_directory_path() / ("test_formal_cfg_" + std::to_string(getpid()));
	fs::remove_all(tmp_proj);
	fs::create_directories(tmp_proj);

	verilog::fpga_target_config cfg;
	cfg.target = "ice40";
	cfg.device = "hx1k";
	cfg.package = "tq144";
	cfg.sby_path = "~/external/oss-cad-suite/bin/sby";

	std::string out_path;
	std::string err;
	assert(verilog::fpga_project_settings::save_to_project(tmp_proj.string(), cfg, out_path, err));

	verilog::fpga_target_config loaded;
	assert(verilog::fpga_project_settings::load_from_project(tmp_proj.string(), loaded, err));
	assert(loaded.sby_path == "~/external/oss-cad-suite/bin/sby");

	// Verify verilog_project_settings_validator accepts sby_path and rejects control chars
	tools::verilog_project_settings_validator vps;
	agentlib::tool_context ctx;
	assert(vps.validate_args(nlohmann::json{{"action", "set"}, {"sby_path", "~/external/oss-cad-suite/bin/sby"}}, ctx, err));
	assert(!vps.validate_args(nlohmann::json{{"action", "set"}, {"sby_path", "bad\x1bpath"}}, ctx, err));

	// Verify format_summary includes sby_path and System EDA Environment formal entries
	verilog::fpga_system_tools sys_tools;
	sys_tools.sby_path = "/opt/oss-cad-suite/bin/sby";
	sys_tools.available_smt_solvers = {"bitwuzla", "z3"};
	std::string summary = verilog::fpga_project_settings::format_summary(loaded, sys_tools, {});
	assert(summary.find("sby_path") != std::string::npos);
	assert(summary.find("SymbiYosys (`sby`)") != std::string::npos);
	assert(summary.find("bitwuzla, z3") != std::string::npos);

	fs::remove_all(tmp_proj);
}

int main()
{
	test_watchdog::setup_watchdog(30);

	test_validator();
	test_find_executable_tilde_expansion();
	test_sby_path_settings_roundtrip();

	std::cout << "PASS: test_verilog_formal\n";
	return 0;
}
