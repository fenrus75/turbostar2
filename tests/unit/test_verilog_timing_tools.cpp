// Tested source file: src/plugins/verilog/verilog_logic_depth_tool.cpp, src/plugins/verilog/verilog_update_timing_tool.cpp, src/plugins/verilog/verilog_module_scanner.cpp
#include "plugins/verilog/verilog_logic_depth_tool.h"
#include "plugins/verilog/verilog_logic_depth_validator.h"
#include "plugins/verilog/verilog_update_timing_tool.h"
#include "plugins/verilog/verilog_update_timing_validator.h"
#include "plugins/verilog/verilog_module_scanner.h"
#include "fs_utils.h"
#include "unit/test_watchdog.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

void test_verilog_logic_depth_validator()
{
	tools::verilog_logic_depth_validator validator;
	assert(validator.get_name() == "verilog_logic_depth");
	assert(validator.get_family() == "verilog");
	assert(!validator.get_description().empty());

	auto schema = validator.get_parameters_schema();
	assert(schema.contains("properties"));
	assert(schema["properties"].contains("module"));
	assert(schema["properties"].contains("target"));
	assert(schema["properties"].contains("top_k"));
	assert(schema["properties"].contains("write_sidecar"));
	assert(schema["properties"].contains("group_by_sink"));

	agentlib::tool_context ctx;
	ctx.fs_security.add_allowed_root(std::filesystem::current_path(), agentlib::access_type::write);

	// Test validation with netlist
	nlohmann::json valid_args = {
		{"module", "main"},
		{"netlist", "examples/test8008/build/netlist.json"},
		{"top_k", 2},
		{"write_sidecar", false}
	};
	std::string err;
	bool ok = validator.validate_args(valid_args, ctx, err);
	assert(ok);
	auto tool = validator.create_tool(valid_args);
	assert(tool != nullptr);
	assert(validator.is_pure());

	// When write_sidecar is true, tool is not pure
	nlohmann::json impure_args = {
		{"module", "main"},
		{"netlist", "examples/test8008/build/netlist.json"},
		{"write_sidecar", true}
	};
	bool ok2 = validator.validate_args(impure_args, ctx, err);
	assert(ok2);
	auto impure_tool = validator.create_tool(impure_args);
	assert(impure_tool != nullptr);
	assert(!validator.is_pure());
}

void test_verilog_logic_depth_execution()
{
	agentlib::tool_context ctx;
	ctx.fs_security.add_allowed_root(std::filesystem::current_path(), agentlib::access_type::write);

	tools::verilog_logic_depth_validator validator;
	std::string err;

	if (std::filesystem::exists("examples/test8008/build/netlist.json")) {
		std::string test_sidecar = "examples/test8008/test_main.timing.md";
		std::error_code ec;
		std::filesystem::remove(test_sidecar, ec);

		nlohmann::json args = {
			{"module", "main"},
			{"file", "examples/test8008/test_main.sv"},
			{"netlist", "examples/test8008/build/netlist.json"},
			{"top_k", 3},
			{"write_sidecar", true}
		};

		bool ok = validator.validate_args(args, ctx, err);
		assert(ok);
		auto tool = validator.create_tool(args);
		assert(tool != nullptr);

		std::string result = tool->execute(ctx);
		assert(result.find("# Timing: `main`") != std::string::npos);
		assert(result.find("## Inputs") != std::string::npos);
		assert(result.find("## Outputs") != std::string::npos);
		assert(result.find("Worst Combinational Paths") != std::string::npos);
		assert(result.find("**Critical Path Summary**:") != std::string::npos);
		assert(result.find("Updated timing sidecar written to") != std::string::npos);

		// Verify sidecar was written to disk
		assert(std::filesystem::exists(test_sidecar));
		std::ifstream ifs(test_sidecar);
		std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
		assert(content.find("# Timing: `main`") != std::string::npos);
		assert(content.find("| `clk` | 1 | - | Clock |") != std::string::npos);

		std::filesystem::remove(test_sidecar, ec);

		if (std::filesystem::exists("examples/test8008/sv/alu/alu.sv")) {
			// Test grouped by sink (default)
			nlohmann::json alu_args = {
				{"module", "alu_mod"},
				{"file", "examples/test8008/sv/alu/alu.sv"},
				{"top_k", 4},
				{"write_sidecar", false},
				{"group_by_sink", true}
			};
			bool alu_ok = validator.validate_args(alu_args, ctx, err);
			assert(alu_ok);
			auto alu_tool = validator.create_tool(alu_args);
			assert(alu_tool != nullptr);
			std::string alu_res = alu_tool->execute(ctx);
			std::cout << "=== ALU LOGIC DEPTH OUTPUT (GROUPED) ===\n" << alu_res << std::endl;
			assert(alu_res.find("**Critical Path Summary**:") != std::string::npos);
			assert(alu_res.find("alu.sv:") != std::string::npos);
			assert(alu_res.find("flags[") != std::string::npos);
			assert(alu_res.find("Converging on") != std::string::npos);
			assert(alu_res.find("all converge here") != std::string::npos);
			assert(alu_res.find("alu_out[7]") != std::string::npos); // Distinct bottleneck 2 now visible!

			// Test raw paths (group_by_sink: false)
			nlohmann::json raw_args = {
				{"module", "alu_mod"},
				{"file", "examples/test8008/sv/alu/alu.sv"},
				{"top_k", 4},
				{"write_sidecar", false},
				{"group_by_sink", false}
			};
			bool raw_ok = validator.validate_args(raw_args, ctx, err);
			assert(raw_ok);
			auto raw_tool = validator.create_tool(raw_args);
			std::string raw_res = raw_tool->execute(ctx);
			assert(raw_res.find("Shared bottleneck") != std::string::npos);
		}
	}
}

void test_verilog_update_timing_validator()
{
	tools::verilog_update_timing_validator validator;
	assert(validator.get_name() == "verilog_update_timing");
	assert(validator.get_family() == "verilog");
	assert(!validator.is_pure()); // Always impure since it writes sidecars
	assert(!validator.get_description().empty());

	auto schema = validator.get_parameters_schema();
	assert(schema.contains("properties"));
	assert(schema["properties"].contains("path"));
	assert(schema["properties"].contains("target"));

	agentlib::tool_context ctx;
	ctx.fs_security.add_allowed_root(std::filesystem::current_path(), agentlib::access_type::write);

	nlohmann::json args = {
		{"path", "examples/test8008"},
		{"target", "Generic LUT4"}
	};
	std::string err;
	bool ok = validator.validate_args(args, ctx, err);
	assert(ok);
	auto tool = validator.create_tool(args);
	assert(tool != nullptr);
}

void test_verilog_update_timing_execution()
{
	agentlib::tool_context ctx;
	ctx.fs_security.add_allowed_root(std::filesystem::current_path(), agentlib::access_type::write);

	// Create a temporary directory with a module, a backup file, and comments
	std::string tmp_dir = "tests/unit/tmp_timing_test";
	std::filesystem::create_directories(tmp_dir);

	std::string mod1_path = tmp_dir + "/counter.sv";
	std::ofstream ofs1(mod1_path);
	ofs1 << "module counter(input logic clk, input logic rst, output logic [3:0] q);\n"
	     << "/* todo: make this a module so the top level is only modules and wires */\n"
	     << "always_ff @(posedge clk) begin\n"
	     << "  if (rst) q <= 0;\n"
	     << "  else q <= q + 1;\n"
	     << "end\n"
	     << "endmodule\n";
	ofs1.close();

	// Create a backup file that should be ignored
	std::string backup_path = tmp_dir + "/counter.sv~";
	std::ofstream ofs_bak(backup_path);
	ofs_bak << "module spurious_backup(input logic clk);\nendmodule\n";
	ofs_bak.close();

	// Create a top module that instantiates counter to verify hierarchy flattening
	std::string mod2_path = tmp_dir + "/top_wrapper.sv";
	std::ofstream ofs2(mod2_path);
	ofs2 << "module top_wrapper(input logic clk, input logic rst, output logic [3:0] count_out);\n"
	     << "  counter u_counter(.clk(clk), .rst(rst), .q(count_out));\n"
	     << "endmodule\n";
	ofs2.close();

	// Create a module with an unassigned (floating) output port to verify "Floating" classification
	std::string mod_flt_path = tmp_dir + "/floating_mod.sv";
	std::ofstream ofs_flt(mod_flt_path);
	ofs_flt << "module floating_mod(input logic clk, output logic [3:0] unassigned_out);\n"
	        << "endmodule\n";
	ofs_flt.close();

	tools::verilog_update_timing_validator validator;
	std::string err;

	if (!fs_utils::find_executable("yosys").empty()) {
		// Test using PascalCase arguments as passed by MCP callers
		nlohmann::json args = {
			{"Path", tmp_dir},
			{"Target", "Generic LUT4"}
		};

		bool ok = validator.validate_args(args, ctx, err);
		assert(ok);
		auto tool = validator.create_tool(args);
		assert(tool != nullptr);

		std::string result = tool->execute(ctx);
		// Verify spurious modules like "so" or "spurious_backup" were NOT discovered
		assert(result.find("so") == std::string::npos);
		assert(result.find("spurious_backup") == std::string::npos);
		assert(result.find("counter") != std::string::npos);
		assert(result.find("top_wrapper") != std::string::npos);

		std::string sidecar_file = tmp_dir + "/counter.timing.md";
		assert(std::filesystem::exists(sidecar_file));

		std::ifstream s_ifs(sidecar_file);
		std::string s_content((std::istreambuf_iterator<char>(s_ifs)), std::istreambuf_iterator<char>());
		assert(s_content.find("# Timing: `counter`") != std::string::npos);
		assert(s_content.find("| `clk` | 1 | - | Clock |") != std::string::npos);
		assert(s_content.find("| `rst` | 1 | 0 | Register |") != std::string::npos);
		assert(s_content.find("| `q` | 4 | 0 | Register |") != std::string::npos);

		// Verify top_wrapper flattened counter and recognizes count_out as driven by Register, not Constant!
		std::string top_sidecar = tmp_dir + "/top_wrapper.timing.md";
		assert(std::filesystem::exists(top_sidecar));
		std::ifstream top_ifs(top_sidecar);
		std::string top_content((std::istreambuf_iterator<char>(top_ifs)), std::istreambuf_iterator<char>());
		assert(top_content.find("# Timing: `top_wrapper`") != std::string::npos);
		assert(top_content.find("| `count_out` | 4 | 0 | Register |") != std::string::npos);

		// Verify floating_mod marks unassigned_out as Floating
		std::string flt_sidecar = tmp_dir + "/floating_mod.timing.md";
		assert(std::filesystem::exists(flt_sidecar));
		std::ifstream flt_ifs(flt_sidecar);
		std::string flt_content((std::istreambuf_iterator<char>(flt_ifs)), std::istreambuf_iterator<char>());
		assert(flt_content.find("# Timing: `floating_mod`") != std::string::npos);
		assert(flt_content.find("| `unassigned_out` | 4 | - | Floating |") != std::string::npos);
	}

	std::error_code ec;
	std::filesystem::remove_all(tmp_dir, ec);
}

void test_verilog_module_scanner()
{
	using verilog::verilog_module_scanner;
	// Ignored file test
	assert(verilog_module_scanner::is_ignored_file("foo.sv~"));
	assert(verilog_module_scanner::is_ignored_file("foo.sv.bak"));
	assert(verilog_module_scanner::is_ignored_file("tb_top.sv"));
	assert(verilog_module_scanner::is_ignored_file("alu_tb.v"));
	assert(verilog_module_scanner::is_ignored_file("tests/testbench/my_mod.sv"));
	assert(!verilog_module_scanner::is_ignored_file("src/alu.sv"));
	assert(!verilog_module_scanner::is_ignored_file("core.v"));

	// Comment stripping test
	std::string code = "module my_mod; // line comment\n"
	                   "/* block\ncomment */\n"
	                   "string s = \"// not a comment /* neither */\";\n"
	                   "endmodule";
	std::string stripped = verilog_module_scanner::strip_comments(code);
	assert(stripped.find("line comment") == std::string::npos);
	assert(stripped.find("block") == std::string::npos);
	assert(stripped.find("// not a comment") != std::string::npos);

	// Test module discovery
	std::string tmp_dir = std::format("tests/unit/tmp_test_mod_scan_{}", (long)getpid());
	std::filesystem::create_directories(tmp_dir);
	std::string sv_file = tmp_dir + "/sample.sv";
	{
		std::ofstream ofs(sv_file);
		ofs << "// module commented_out(input a);\n";
		ofs << "module alpha(input a, output b); endmodule\n";
		ofs << "module beta(input c, output d); endmodule\n";
	}

	std::vector<std::string> files = {sv_file};
	auto mods = verilog_module_scanner::discover_modules_in_files(files);
	assert(mods.size() == 2);
	assert(mods[0].module_name == "alpha");
	assert(mods[0].sidecar_path == tmp_dir + "/alpha.timing.md");
	assert(mods[1].module_name == "beta");
	assert(mods[1].sidecar_path == tmp_dir + "/beta.timing.md");

	std::vector<std::string> out_files;
	auto scanned = verilog_module_scanner::scan(tmp_dir, out_files);
	assert(scanned.size() == 2);
	assert(out_files.size() == 1);

	std::error_code ec;
	std::filesystem::remove_all(tmp_dir, ec);
}

void test_standalone_verilog_update_timing_cli()
{
	std::string cli_bin = "build/turbostar-verilog-update-timing";
	if (!std::filesystem::exists(cli_bin)) {
		return;
	}
	// Test --help
	std::string help_out = fs_utils::execute_command_sync(cli_bin + " --help");
	assert(help_out.find("TurboStar Verilog Timing Sidecar Updater") != std::string::npos);

	// Test dry-run on synthetic directory
	std::string tmp_dir = std::format("tests/unit/tmp_test_vut_cli_{}", (long)getpid());
	std::filesystem::create_directories(tmp_dir);
	std::string sv_file = tmp_dir + "/simple_mod.sv";
	{
		std::ofstream ofs(sv_file);
		ofs << "module simple_mod(input clk, input d, output reg q);\n"
		       "  always @(posedge clk) q <= d;\n"
		       "endmodule\n";
	}

	std::string run_cmd = std::format("{} {} --dry-run", cli_bin, tmp_dir);
	std::string out = fs_utils::execute_command_sync(run_cmd);
	assert(out.find("simple_mod") != std::string::npos);
	assert(out.find("DRY-RUN") != std::string::npos);

	std::error_code ec;
	std::filesystem::remove_all(tmp_dir, ec);
}

int main()
{
	test_watchdog::setup_watchdog();
	std::cout << "Testing verilog logic depth and update timing tools..." << std::endl;

	test_verilog_module_scanner();
	test_verilog_logic_depth_validator();
	test_verilog_logic_depth_execution();
	test_verilog_update_timing_validator();
	test_verilog_update_timing_execution();
	test_standalone_verilog_update_timing_cli();

	std::cout << "All verilog timing tool tests passed successfully!" << std::endl;
	return 0;
}
