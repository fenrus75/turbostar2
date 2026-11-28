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

static void test_unchecked_output_ports_audit()
{
	fs::path tmp_proj = fs::temp_directory_path() / ("test_formal_out_audit_" + std::to_string(getpid()));
	fs::remove_all(tmp_proj);
	fs::create_directories(tmp_proj / "src");
	fs::create_directories(tmp_proj / "formal");

	fs::path rtl_file = tmp_proj / "src" / "demo_unit.sv";
	{
		std::ofstream ofs(rtl_file);
		ofs << "module demo_unit (\n"
		    << "    input  logic       clk,\n"
		    << "    input  logic       rst,\n"
		    << "    output logic       valid,\n"
		    << "    output logic [7:0] data,\n"
		    << "    output logic       err,\n"
		    << "    output logic       busy\n"
		    << ");\n"
		    << "    assign valid = 1'b0;\n"
		    << "    assign data  = 8'h00;\n"
		    << "    assign err   = 1'b0;\n"
		    << "    assign busy  = 1'b0;\n"
		    << "endmodule\n";
	}

	fs::path props_file = tmp_proj / "formal" / "demo_unit_props.sv";
	{
		std::ofstream ofs(props_file);
		ofs << "module demo_unit_props (\n"
		    << "    input logic       clk,\n"
		    << "    input logic       rst,\n"
		    << "    input logic       valid,\n"
		    << "    input logic [7:0] data_p,\n"
		    << "    input logic       err,\n"
		    << "    input logic       busy\n"
		    << ");\n"
		    << "    logic data_ok;\n"
		    << "    assign data_ok = (data_p == 8'h00);\n"
		    << "    always @(posedge clk) begin\n"
		    << "        if (rst) assert (!valid) else $error(\"err=%b\", err);\n"
		    << "        assert (data_ok);\n"
		    << "        cover (busy);\n"
		    << "    end\n"
		    << "endmodule\n"
		    << "bind demo_unit demo_unit_props u_props (.*, .data_p(data));\n";
	}

	std::vector<std::string> rtl_files = {rtl_file.string()};
	std::vector<std::string> formal_files = {props_file.string()};

	// valid is asserted directly; data is asserted via helper `data_ok` + `.data_p(data)` bind remap.
	// err is only in `$error(...)` and busy is only in `cover(...)`, so both must be flagged!
	auto unchecked = tools::verilog_formal_tool::find_unchecked_output_ports("demo_unit", rtl_files, formal_files);
	assert(unchecked.size() == 2);
	assert(unchecked[0] == "err");
	assert(unchecked[1] == "busy");

	// Now pin down err and busy too (with assume but no cover) -> 0 unchecked outputs, but assumes_without_covers is true
	{
		std::ofstream ofs(props_file, std::ios::trunc);
		ofs << "module demo_unit_props (\n"
		    << "    input logic       clk,\n"
		    << "    input logic       rst,\n"
		    << "    input logic       valid,\n"
		    << "    input logic [7:0] data_p,\n"
		    << "    input logic       err,\n"
		    << "    input logic       busy\n"
		    << ");\n"
		    << "    always @(posedge clk) begin\n"
		    << "        assume (!rst);\n"
		    << "        // cover (busy); -- commented out, must not count\n"
		    << "        assert (!valid && !err && !busy && data_p == 8'h00);\n"
		    << "    end\n"
		    << "endmodule\n"
		    << "bind demo_unit demo_unit_props u_props (.*, .data_p(data));\n";
	}
	auto unchecked2 = tools::verilog_formal_tool::find_unchecked_output_ports("demo_unit", rtl_files, formal_files);
	assert(unchecked2.empty());
	assert(tools::verilog_formal_tool::has_assumes_without_covers(formal_files));

	// Adding a real cover() clears the assumes_without_covers flag
	{
		std::ofstream ofs(props_file, std::ios::trunc);
		ofs << "module demo_unit_props (input logic clk, input logic rst, input logic busy);\n"
		    << "    always @(posedge clk) begin\n"
		    << "        assume (!rst);\n"
		    << "        cover (busy);\n"
		    << "    end\n"
		    << "endmodule\n";
	}
	assert(!tools::verilog_formal_tool::has_assumes_without_covers(formal_files));

	fs::remove_all(tmp_proj);
}

static void test_formal_comment_pragmas_and_precedence()
{
	fs::path tmp_proj = fs::temp_directory_path() / ("test_formal_pragmas_" + std::to_string(getpid()));
	fs::remove_all(tmp_proj);
	fs::create_directories(tmp_proj / "formal");

	fs::path pfile = tmp_proj / "formal" / "div_mod_props.sv";
	{
		std::ofstream ofs(pfile);
		ofs << "// formal: depth=26 timeout=90 unknown_key=123 // initial estimate\n"
		    << "  // FORMAL: DEPTH=30 solver=BITWUZLA mode=prove+cover // 16-bit divider takes 24+ cycles\n"
		    << "module div_mod_props (input logic clk);\n"
		    << "endmodule\n";
	}

	std::vector<std::string> files = {pfile.string()};
	auto pragma = tools::verilog_formal_tool::parse_formal_pragmas(files);
	assert(pragma.depth.has_value() && *pragma.depth == 30);
	assert(pragma.timeout_sec.has_value() && *pragma.timeout_sec == 90);
	assert(pragma.solver.has_value() && *pragma.solver == "bitwuzla");
	assert(pragma.mode.has_value() && *pragma.mode == "prove+cover");

	// Precedence 1: omitted tool args -> pragma overrides defaults
	tools::verilog_formal_args omitted_args;
	int eff_depth = 0;
	bool from_pragma = false;
	int eff_timeout = 0;
	std::string eff_solver, eff_mode;
	tools::verilog_formal_tool::resolve_module_formal_config(
		omitted_args, pragma, eff_depth, from_pragma, eff_timeout, eff_solver, eff_mode
	);
	assert(eff_depth == 30 && from_pragma);
	assert(eff_timeout == 90);
	assert(eff_solver == "bitwuzla");
	assert(eff_mode == "prove+cover");

	// Precedence 2: explicit tool args (even if equal to default 20!) override pragma
	tools::verilog_formal_args explicit_args;
	explicit_args.depth = 20;
	explicit_args.depth_explicit = true;
	explicit_args.timeout_sec = 45;
	explicit_args.timeout_explicit = true;
	explicit_args.solver = "z3";
	explicit_args.solver_explicit = true;
	explicit_args.mode = "bmc";
	explicit_args.mode_explicit = true;
	tools::verilog_formal_tool::resolve_module_formal_config(
		explicit_args, pragma, eff_depth, from_pragma, eff_timeout, eff_solver, eff_mode
	);
	assert(eff_depth == 20 && !from_pragma);
	assert(eff_timeout == 45);
	assert(eff_solver == "z3");
	assert(eff_mode == "bmc");

	// Clamping test
	{
		std::ofstream ofs(pfile, std::ios::trunc);
		ofs << "// formal: depth=9999 timeout=1 solver=bogus mode=bogus\n";
	}
	auto clamped = tools::verilog_formal_tool::parse_formal_pragmas(files);
	assert(clamped.depth.has_value() && *clamped.depth == 500);
	assert(clamped.timeout_sec.has_value() && *clamped.timeout_sec == 5);
	assert(!clamped.solver.has_value());
	assert(!clamped.mode.has_value());

	fs::remove_all(tmp_proj);
}

int main()
{
	test_watchdog::setup_watchdog(30);

	test_validator();
	test_find_executable_tilde_expansion();
	test_sby_path_settings_roundtrip();
	test_unchecked_output_ports_audit();
	test_formal_comment_pragmas_and_precedence();

	std::cout << "PASS: test_verilog_formal\n";
	return 0;
}
