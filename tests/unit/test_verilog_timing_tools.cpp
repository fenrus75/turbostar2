// Tested source file: src/plugins/verilog/verilog_logic_depth_tool.cpp, src/plugins/verilog/verilog_update_timing_tool.cpp, src/plugins/verilog/verilog_module_scanner.cpp, src/plugins/verilog/fpga_project_settings.cpp, src/plugins/verilog/nextpnr_timing_analyzer.cpp, src/plugins/verilog/verilog_project_settings_tool.cpp, src/plugins/verilog/verilog_pnr_timing_tool.cpp
#include "plugins/verilog/verilog_logic_depth_tool.h"
#include "plugins/verilog/verilog_logic_depth_validator.h"
#include "plugins/verilog/verilog_update_timing_tool.h"
#include "plugins/verilog/verilog_update_timing_validator.h"
#include "plugins/verilog/verilog_module_scanner.h"
#include "plugins/verilog/fpga_project_settings.h"
#include "plugins/verilog/nextpnr_timing_analyzer.h"
#include "plugins/verilog/verilog_project_settings_tool.h"
#include "plugins/verilog/verilog_project_settings_validator.h"
#include "plugins/verilog/verilog_pnr_timing_tool.h"
#include "plugins/verilog/verilog_pnr_timing_validator.h"
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
	assert(!schema["properties"].contains("write_sidecar"));
	assert(schema["properties"].contains("group_by_sink"));

	agentlib::tool_context ctx;
	ctx.fs_security.add_allowed_root(std::filesystem::current_path(), agentlib::access_type::write);

	// Test validation with netlist
	nlohmann::json valid_args = {
		{"module", "main"},
		{"netlist", "examples/test8008/build/netlist.json"},
		{"top_k", 2}
	};
	std::string err;
	bool ok = validator.validate_args(valid_args, ctx, err);
	assert(ok);
	auto tool = validator.create_tool(valid_args);
	assert(tool != nullptr);
	assert(validator.is_pure());
}

void test_verilog_logic_depth_execution()
{
	agentlib::tool_context ctx;
	ctx.fs_security.add_allowed_root(std::filesystem::current_path(), agentlib::access_type::write);

	tools::verilog_logic_depth_validator validator;
	std::string err;

	if (std::filesystem::exists("examples/test8008/build/netlist.json")) {
		nlohmann::json args = {
			{"module", "main"},
			{"file", "examples/test8008/test_main.sv"},
			{"netlist", "examples/test8008/build/netlist.json"},
			{"top_k", 3}
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

		if (std::filesystem::exists("examples/test8008/sv/alu/alu.sv")) {
			// Test grouped by sink (default)
			nlohmann::json alu_args = {
				{"module", "alu_mod"},
				{"file", "examples/test8008/sv/alu/alu.sv"},
				{"top_k", 4},
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
			assert(alu_res.find("alu_out") != std::string::npos);

			// Test raw paths (group_by_sink: false)
			nlohmann::json raw_args = {
				{"module", "alu_mod"},
				{"file", "examples/test8008/sv/alu/alu.sv"},
				{"top_k", 4},
				{"group_by_sink", false}
			};
			bool raw_ok = validator.validate_args(raw_args, ctx, err);
			assert(raw_ok);
			auto raw_tool = validator.create_tool(raw_args);
			std::string raw_res = raw_tool->execute(ctx);
			assert(raw_res.find("Shared bottleneck") != std::string::npos);

			// Test zero-arg / default invocation using turbostar.json
			std::string tmp_prj = std::format("tests/unit/tmp_ld_prj_{}", (long)getpid());
			std::filesystem::create_directories(tmp_prj);
			std::string cfg_path;
			verilog::fpga_target_config def_cfg;
			def_cfg.target = "ice40";
			def_cfg.top_module = "main";
			def_cfg.sv_path = "sv";
			verilog::fpga_project_settings::save_to_project(tmp_prj, def_cfg, cfg_path, err);

			std::error_code ec_sym;
			std::filesystem::create_directory_symlink(std::filesystem::canonical("examples/test8008/sv"),
			                                         std::filesystem::path(tmp_prj) / "sv", ec_sym);

			agentlib::tool_context ctx_def;
			ctx_def.fs_security.add_allowed_root(std::filesystem::current_path(), agentlib::access_type::write);
			ctx_def.fs_security.set_working_directory(tmp_prj);

			nlohmann::json empty_args = nlohmann::json::object();
			bool ok_def = validator.validate_args(empty_args, ctx_def, err);
			assert(ok_def);
			auto tool_def = validator.create_tool(empty_args);
			assert(tool_def != nullptr);
			std::string res_def = tool_def->execute(ctx_def);
			assert(res_def.find("# Timing: `main`") != std::string::npos);
			assert(res_def.find("iCE40") != std::string::npos);

			std::filesystem::remove_all(tmp_prj, ec_sym);
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

void test_fpga_project_settings()
{
	// 1. Detect tools
	auto sys_tools = verilog::fpga_project_settings::detect_system_tools();
	assert(!sys_tools.yosys_path.empty());
	assert(!sys_tools.nextpnr_ice40_path.empty());
	assert(std::find(sys_tools.available_targets.begin(), sys_tools.available_targets.end(), "ice40") != sys_tools.available_targets.end());

	// 2. Scan PCF in test8008
	auto pcfs = verilog::fpga_project_settings::scan_pcf_files("examples/test8008");
	assert(!pcfs.empty());
	bool found_cu = false;
	for (const auto &p : pcfs) {
		if (p.find("cu.pcf") != std::string::npos) found_cu = true;
	}
	assert(found_cu);

	// Detect sv_path
	assert(verilog::fpga_project_settings::detect_sv_path("examples/test8008") == "sv");

	// 3. Save and load from temporary project directory
	std::string tmp_dir = std::format("tests/unit/tmp_fpga_cfg_{}", (long)getpid());
	std::filesystem::create_directories(tmp_dir);

	verilog::fpga_target_config cfg;
	cfg.target = "ice40";
	cfg.device = "hx8k";
	cfg.package = "cb132";
	cfg.top_module = "main";
	cfg.freq_mhz = 60.0;
	cfg.pcf_file = "pcf/cu.pcf";
	cfg.sv_path = "sv";

	std::string path_used;
	std::string err;
	bool save_ok = verilog::fpga_project_settings::save_to_project(tmp_dir, cfg, path_used, err);
	assert(save_ok);
	assert(path_used.find("turbostar.json") != std::string::npos);
	assert(std::filesystem::exists(path_used));

	verilog::fpga_target_config loaded_cfg;
	bool load_ok = verilog::fpga_project_settings::load_from_project(tmp_dir, loaded_cfg, err);
	assert(load_ok);
	assert(loaded_cfg.target == "ice40");
	assert(loaded_cfg.device == "hx8k");
	assert(loaded_cfg.package == "cb132");
	assert(loaded_cfg.top_module == "main");
	assert(loaded_cfg.freq_mhz == 60.0);
	assert(loaded_cfg.pcf_file == "pcf/cu.pcf");
	assert(loaded_cfg.sv_path == "sv");

	std::vector<std::string> missing;
	assert(verilog::fpga_project_settings::is_complete(loaded_cfg, missing));

	// Test missing guidance
	verilog::fpga_target_config partial_cfg;
	assert(!verilog::fpga_project_settings::is_complete(partial_cfg, missing));
	assert(missing.size() == 4); // target, device, top_module, freq_mhz
	std::string guidance = verilog::fpga_project_settings::format_guidance(tmp_dir, partial_cfg, sys_tools, pcfs);
	assert(guidance.find("Missing FPGA Project Configuration") != std::string::npos);
	assert(guidance.find("Target Architecture") != std::string::npos);
	assert(guidance.find("Device & Package") != std::string::npos);

	std::error_code ec;
	std::filesystem::remove_all(tmp_dir, ec);
}

void test_verilog_project_settings_tool()
{
	tools::verilog_project_settings_validator validator;
	assert(validator.get_name() == "verilog_project_settings");
	assert(!validator.is_pure());

	std::string tmp_dir = std::format("tests/unit/tmp_vps_tool_{}", (long)getpid());
	std::filesystem::create_directories(tmp_dir);

	agentlib::tool_context ctx;
	ctx.fs_security.add_allowed_root(std::filesystem::current_path(), agentlib::access_type::write);
	ctx.fs_security.set_working_directory(tmp_dir);

	// 1. Initial GET should show guidance for missing settings
	nlohmann::json get_args = {{"action", "get"}};
	std::string err;
	assert(validator.validate_args(get_args, ctx, err));
	auto tool = validator.create_tool(get_args);
	assert(tool != nullptr);
	std::string get_out = tool->execute(ctx);
	assert(get_out.find("FPGA Project Settings") != std::string::npos);
	assert(get_out.find("Missing FPGA Project Configuration") != std::string::npos);

	// 2. SET action creates turbostar.json
	nlohmann::json set_args = {
		{"action", "set"},
		{"target", "ice40"},
		{"device", "hx8k"},
		{"package", "cb132"},
		{"top_module", "main"},
		{"freq_mhz", 60.0},
		{"pcf_file", "pcf/cu.pcf"},
		{"default_sv_path", "sv"}
	};
	assert(validator.validate_args(set_args, ctx, err));
	auto set_tool = validator.create_tool(set_args);
	std::string set_out = set_tool->execute(ctx);
	assert(set_out.find("Successfully saved FPGA project configuration") != std::string::npos);
	assert(std::filesystem::exists(tmp_dir + "/turbostar.json"));

	// 3. Second GET shows configured status without missing guidance
	auto tool2 = validator.create_tool(get_args);
	std::string get_out2 = tool2->execute(ctx);
	assert(get_out2.find("Missing FPGA Project Configuration") == std::string::npos);
	assert(get_out2.find("`ice40`") != std::string::npos);
	assert(get_out2.find("60.0 MHz") != std::string::npos);
	assert(get_out2.find("`sv`") != std::string::npos);

	std::error_code ec;
	std::filesystem::remove_all(tmp_dir, ec);
}

void test_nextpnr_timing_analyzer()
{
	std::string timing_file = "examples/test8008/synth/timing.json";
	std::string netlist_file = "examples/test8008/synth/main.json";

	if (!std::filesystem::exists(timing_file) || !std::filesystem::exists(netlist_file)) {
		return;
	}

	verilog::nextpnr_timing_analyzer analyzer;
	std::string err;
	bool ok = analyzer.load_from_files(timing_file, netlist_file, err);
	assert(ok);

	// 1. Clock domains
	const auto &domains = analyzer.clock_domains();
	assert(!domains.empty());
	assert(domains.front().achieved_mhz > 60.0);
	assert(domains.front().met);

	// 2. Hardware utilization
	const auto &util = analyzer.utilization();
	assert(!util.empty());
	bool found_lc = false;
	for (const auto &u : util) {
		if (u.resource_name == "ICESTORM_LC") {
			found_lc = true;
			assert(u.used == 795);
			assert(u.available == 7680);
		}
	}
	assert(found_lc);

	// 3. Filtered Reg -> Reg paths
	auto reg_paths = analyzer.get_filtered_paths(true, 5);
	assert(reg_paths.size() == 1);
	const auto &top_p = reg_paths.front();
	assert(top_p.path_type == verilog::timing_path_type::reg_to_reg);
	assert(top_p.total_delay_ns > 14.0 && top_p.total_delay_ns < 15.0);
	assert(top_p.logic_delay_ns > 4.0);
	assert(top_p.routing_delay_ns > 9.0);

	// Check RTL anchors
	assert(top_p.start_src.find("decode_mod.sv") != std::string::npos);
	assert(top_p.end_src.find("register.sv") != std::string::npos);

	// 4. Sink-grouped bottlenecks
	auto bottlenecks = analyzer.get_sink_grouped_bottlenecks(true, 5);
	assert(bottlenecks.size() == 1);
	assert(bottlenecks.front().endpoint.find("C_reg") != std::string::npos);

	// 5. Generate Markdown
	std::string md = analyzer.generate_timing_markdown(true, true, 3);
	assert(md.find("Clock Domain & Frequency Timing") != std::string::npos);
	assert(md.find("Hardware Resource Utilization") != std::string::npos);
	assert(md.find("Physical Critical Paths") != std::string::npos);
	assert(md.find("decode_mod.sv") != std::string::npos);
	assert(md.find("register.sv") != std::string::npos);
}

void test_verilog_pnr_timing_tool()
{
	tools::verilog_pnr_timing_validator validator;
	assert(validator.get_name() == "verilog_pnr_timing");
	assert(validator.is_pure());

	std::string timing_file = "examples/test8008/synth/timing.json";
	std::string netlist_file = "examples/test8008/synth/main.json";
	if (!std::filesystem::exists(timing_file)) {
		return;
	}

	agentlib::tool_context ctx;
	ctx.fs_security.add_allowed_root(std::filesystem::current_path(), agentlib::access_type::read);

	nlohmann::json args = {
		{"timing_report", timing_file},
		{"netlist", netlist_file},
		{"top_k", 2},
		{"path_filter", "reg_to_reg"}
	};

	std::string err;
	assert(validator.validate_args(args, ctx, err));
	auto tool = validator.create_tool(args);
	assert(tool != nullptr);

	std::string res = tool->execute(ctx);
	assert(res.find("Clock Domain & Frequency Timing") != std::string::npos);
	assert(res.find("Hardware Resource Utilization") != std::string::npos);
	assert(res.find("Critical Path Summary") != std::string::npos);
}

void test_verilog_pnr_timing_on_demand()
{
	tools::verilog_pnr_timing_validator validator;
	agentlib::tool_context ctx;
	ctx.fs_security.add_allowed_root(std::filesystem::current_path(), agentlib::access_type::write);

	// Create temporary project configured for test8008
	std::string tmp_dir = std::format("tests/unit/tmp_pnr_ondemand_{}", (long)getpid());
	std::filesystem::create_directories(tmp_dir);

	verilog::fpga_target_config cfg;
	cfg.target = "ice40";
	cfg.device = "hx8k";
	cfg.package = "cb132";
	cfg.top_module = "main";
	cfg.freq_mhz = 60.0;
	cfg.pcf_file = std::filesystem::canonical("examples/test8008/pcf/cu.pcf").string();
	cfg.sv_path = "sv";

	std::string path_used;
	std::string err;
	verilog::fpga_project_settings::save_to_project(tmp_dir, cfg, path_used, err);

	std::error_code ec_sym;
	std::filesystem::create_directory_symlink(std::filesystem::canonical("examples/test8008/sv"),
	                                         std::filesystem::path(tmp_dir) / "sv", ec_sym);

	ctx.fs_security.set_working_directory(tmp_dir);

	nlohmann::json args = nlohmann::json::object();
	assert(validator.validate_args(args, ctx, err));
	auto tool = validator.create_tool(args);
	assert(tool != nullptr);

	std::string res = tool->execute(ctx);
	std::filesystem::remove_all(tmp_dir, ec_sym);


	// The bug produces "Can't guess frontend for input file" / "Yosys synthesis failed"
	assert(res.find("Can't guess frontend") == std::string::npos);
	assert(res.find("Yosys synthesis failed") == std::string::npos);
	assert(res.find("Clock Domain & Frequency Timing") != std::string::npos);
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
	test_fpga_project_settings();
	test_verilog_project_settings_tool();
	test_nextpnr_timing_analyzer();
	test_verilog_pnr_timing_tool();
	test_verilog_pnr_timing_on_demand();

	std::cout << "All verilog timing tool tests passed successfully!" << std::endl;
	return 0;
}
