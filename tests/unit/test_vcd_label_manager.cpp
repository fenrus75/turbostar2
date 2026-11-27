// Tested source file: src/plugins/verilog/vcd_label_manager.cpp
#include "agentlib/tool_context.h"
#include "plugins/verilog/vcd_label_manager.h"
#include "plugins/verilog/vcd_signal_labels_tool.h"
#include "plugins/verilog/vcd_signal_labels_validator.h"
#include "plugins/verilog/vcd_signal_qualifier_tool.h"
#include "plugins/verilog/vcd_signal_qualifier_validator.h"
#include "test_watchdog.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

int main()
{
	test_watchdog::setup_watchdog();

	namespace fs = std::filesystem;

	// 1. Direct normalize_hex_key tests with stray surrounding quotes
	assert(verilog::vcd_label_manager::normalize_hex_key("\"0x00\"") == "0x00");
	assert(verilog::vcd_label_manager::normalize_hex_key("'0x06'") == "0x06");
	assert(verilog::vcd_label_manager::normalize_hex_key(" \"0xFF\" ") == "0xff");
	assert(verilog::vcd_label_manager::normalize_hex_key("\"b01???01\"") == "b01???01");
	assert(verilog::vcd_label_manager::normalize_hex_key("\\\"0x0a\\\"") == "0x0a");

	// 2. End-to-end vcd_signal_labels & vcd_signal_qualifier with extra-quoted keys
	fs::path q_proj = fs::temp_directory_path() / "turbostar_vcd_quoted_keys_unit_test";
	fs::remove_all(q_proj);
	fs::create_directories(q_proj);

	fs::path q_vcd = q_proj / "dump.vcd";
	{
		std::ofstream ofs(q_vcd);
		ofs << "$timescale 1ns $end\n"
		    << "$scope module tb $end\n"
		    << "$var wire 8 ! opcode $end\n"
		    << "$var wire 8 \" wdata $end\n"
		    << "$var wire 1 # we $end\n"
		    << "$var wire 8 $ rdata $end\n"
		    << "$var wire 1 % re_n $end\n"
		    << "$upscope $end\n"
		    << "$enddefinitions $end\n"
		    << "#0\nb00000000 !\nb00000000 \"\n0#\nb00000000 $\n1%\n";
	}

	auto &lm = verilog::vcd_label_manager::get_instance();
	lm.clear_all();

	agentlib::tool_context q_ctx;
	q_ctx.fs_security.set_working_directory(q_proj.string());
	q_ctx.fs_security.add_allowed_root(q_proj.string(), agentlib::access_type::write);

	// 2a. vcd_signal_labels schema exposes additionalProperties: {type: string} for labels map
	tools::vcd_signal_labels_validator lbl_val;
	auto lbl_schema = lbl_val.get_parameters_schema();
	assert(lbl_schema["properties"]["labels"].contains("additionalProperties"));
	assert(lbl_schema["properties"]["labels"]["additionalProperties"]["type"] == "string");

	nlohmann::json lbl_args = {
		{"path", "dump.vcd"},
		{"signal", "\"opcode\""},
		{"labels", {
			{"\"0x00\"", "NOP"},
			{"'0x06'", "\"LAI\""},
			{"\"b01???01\"", "ALU"}
		}}
	};
	std::string err;
	assert(lbl_val.validate_args(lbl_args, q_ctx, err));
	lbl_val.create_tool(lbl_args)->execute(q_ctx);

	// 2b. vcd_signal_qualifier with extra-quoted bindings keys/values
	tools::vcd_signal_qualifier_validator qual_val;
	nlohmann::json qual_args = {
		{"path", "dump.vcd"},
		{"bindings", {
			{"\"wdata\"", "\"we\""},
			{"'rdata'", "'~re_n'"}
		}}
	};
	assert(qual_val.validate_args(qual_args, q_ctx, err));
	qual_val.create_tool(qual_args)->execute(q_ctx);

	// Verify serialized vcd_labels.json has clean unquoted keys and values
	fs::path sidecar = q_proj / "vcd_labels.json";
	assert(fs::exists(sidecar));
	nlohmann::json saved;
	{
		std::ifstream ifs(sidecar);
		ifs >> saved;
	}
	assert(saved.contains("labels") && saved["labels"].contains("opcode"));
	const auto &op_map = saved["labels"]["opcode"];
	assert(op_map.contains("0x00") && op_map["0x00"] == "NOP");
	assert(op_map.contains("0x06") && op_map["0x06"] == "LAI");
	assert(op_map.contains("b01???01") && op_map["b01???01"] == "ALU");
	assert(!op_map.contains("\"0x00\""));
	assert(!op_map.contains("'0x06'"));
	assert(!op_map.contains("\"b01???01\""));

	assert(saved.contains("qualifiers"));
	const auto &q_map = saved["qualifiers"];
	assert(q_map.contains("wdata") && q_map["wdata"] == "we");
	assert(q_map.contains("rdata") && q_map["rdata"] == "~re_n");
	assert(!q_map.contains("\"wdata\""));
	assert(!q_map.contains("'rdata'"));

	// Verify runtime lookups work cleanly
	assert(lm.find_label(q_vcd.string(), "tb.opcode", "0x00", "00000000") == "NOP");
	assert(lm.find_label(q_vcd.string(), "tb.opcode", "0x06", "00000110") == "LAI");
	auto [w_qual, w_low] = lm.find_qualifier(q_vcd.string(), "tb.wdata");
	assert(w_qual == "we" && !w_low);
	auto [r_qual, r_low] = lm.find_qualifier(q_vcd.string(), "tb.rdata");
	assert(r_qual == "re_n" && r_low);

	// 3. Loading a pre-existing vcd_labels.json with already-quoted keys still normalizes cleanly
	lm.clear_all();
	{
		std::ofstream ofs(sidecar, std::ios::trunc);
		ofs << R"({
  "labels": {
    "\"opcode\"": {
      "\"0x10\"": "\"JMP\""
    }
  },
  "qualifiers": {
    "\"wdata\"": "\"we\""
  }
})" << "\n";
	}
	assert(lm.find_label(q_vcd.string(), "tb.opcode", "0x10", "00010000") == "JMP");
	auto [w_qual2, w_low2] = lm.find_qualifier(q_vcd.string(), "tb.wdata");
	assert(w_qual2 == "we" && !w_low2);

	fs::remove_all(q_proj);
	std::cout << "All VCD label manager unit tests passed!\n";
	return 0;
}
