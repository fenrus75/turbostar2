// Tested source file: src/plugins/verilog/verilog_logic_depth_tool.cpp, src/plugins/verilog/verilog_update_timing_tool.cpp
#include "plugins/verilog/verilog_logic_depth_tool.h"
#include "plugins/verilog/verilog_logic_depth_validator.h"
#include "plugins/verilog/verilog_update_timing_tool.h"
#include "plugins/verilog/verilog_update_timing_validator.h"
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
		assert(result.find("Updated timing sidecar written to") != std::string::npos);

		// Verify sidecar was written to disk
		assert(std::filesystem::exists(test_sidecar));
		std::ifstream ifs(test_sidecar);
		std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
		assert(content.find("# Timing: `main`") != std::string::npos);
		assert(content.find("| `clk` | 1 | - | Clock |") != std::string::npos);

		std::filesystem::remove(test_sidecar, ec);
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

	// Create a temporary directory with a couple simple modules
	std::string tmp_dir = "tests/unit/tmp_timing_test";
	std::filesystem::create_directories(tmp_dir);

	std::string mod1_path = tmp_dir + "/counter.sv";
	std::ofstream ofs1(mod1_path);
	ofs1 << "module counter(input logic clk, input logic rst, output logic [3:0] q);\n"
	     << "always_ff @(posedge clk) begin\n"
	     << "  if (rst) q <= 0;\n"
	     << "  else q <= q + 1;\n"
	     << "end\n"
	     << "endmodule\n";
	ofs1.close();

	tools::verilog_update_timing_validator validator;
	std::string err;

	if (!fs_utils::find_executable("yosys").empty()) {
		nlohmann::json args = {
			{"path", tmp_dir},
			{"target", "Generic LUT4"}
		};

		bool ok = validator.validate_args(args, ctx, err);
		assert(ok);
		auto tool = validator.create_tool(args);
		assert(tool != nullptr);

		std::string result = tool->execute(ctx);
		assert(result.find("Timing Sidecars Updated") != std::string::npos);
		assert(result.find("counter") != std::string::npos);

		std::string sidecar_file = tmp_dir + "/counter.timing.md";
		assert(std::filesystem::exists(sidecar_file));

		std::ifstream s_ifs(sidecar_file);
		std::string s_content((std::istreambuf_iterator<char>(s_ifs)), std::istreambuf_iterator<char>());
		assert(s_content.find("# Timing: `counter`") != std::string::npos);
		assert(s_content.find("| `clk` | 1 | - | Clock |") != std::string::npos);
		assert(s_content.find("| `rst` | 1 | 0 | Register |") != std::string::npos);
		assert(s_content.find("| `q` | 4 | 0 | Register |") != std::string::npos);
	}

	std::error_code ec;
	std::filesystem::remove_all(tmp_dir, ec);
}

int main()
{
	test_watchdog::setup_watchdog();
	std::cout << "Testing verilog logic depth and update timing tools..." << std::endl;

	test_verilog_logic_depth_validator();
	test_verilog_logic_depth_execution();
	test_verilog_update_timing_validator();
	test_verilog_update_timing_execution();

	std::cout << "All verilog timing tool tests passed successfully!" << std::endl;
	return 0;
}
