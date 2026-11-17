// Tested source file: src/plugins/verilog/vcd_axi_protocol_checker.cpp src/plugins/verilog/vcd_protocol_analyzer.cpp
#include "plugins/verilog/vcd_compare_tool.h"
#include "plugins/verilog/vcd_compare_validator.h"
#include "plugins/verilog/vcd_file_info_tool.h"
#include "plugins/verilog/vcd_file_info_validator.h"
#include "plugins/verilog/vcd_parser.h"
#include "plugins/verilog/vcd_protocol_analyzer.h"
#include "plugins/verilog/vcd_wave_tool.h"
#include "plugins/verilog/vcd_wave_validator.h"
#include "test_watchdog.h"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace {

std::string write_vcd(const std::filesystem::path &dir, std::string_view name, std::string_view content)
{
	std::filesystem::path p = dir / name;
	std::ofstream out(p, std::ios::binary | std::ios::trunc);
	assert(out.is_open());
	out << content;
	return p.string();
}

void test_no_false_positives_on_non_axi(const std::filesystem::path &dir)
{
	std::string path = write_vcd(dir, "non_axi.vcd", R"($timescale 1ns $end
$scope module tb $end
$var wire 1 ! clk $end
$var wire 1 " valid $end
$var wire 8 # data $end
$upscope $end
$enddefinitions $end
#0
0!
0"
b00000000 #
#5
1!
1"
b10101010 #
#10
0!
#15
1!
0"
)");
	verilog::vcd_parser parser;
	std::string err;
	assert(parser.parse_all(path, err));
	verilog::vcd_protocol_analyzer analyzer;
	auto report = analyzer.analyze(parser);
	assert(report.interfaces.empty());
	assert(report.total_violations() == 0);
}

void test_axi_stream_clean_and_violations(const std::filesystem::path &dir)
{
	std::string clean_path = write_vcd(dir, "axis_clean.vcd", R"($timescale 1ns $end
$scope module tb $end
$var wire 1 ! aclk $end
$var wire 1 " aresetn $end
$var wire 1 # m_axis_tvalid $end
$var wire 1 $ m_axis_tready $end
$var wire 8 % m_axis_tdata $end
$upscope $end
$enddefinitions $end
#0
0!
0"
0#
0$
b00000000 %
#5
1!
#10
0!
1"
#15
1!
1#
0$
b10101010 %
#20
0!
#25
1!
1$
#30
0!
#35
1!
0#
)");
	verilog::vcd_parser clean_parser;
	std::string err;
	assert(clean_parser.parse_all(clean_path, err));
	verilog::vcd_protocol_analyzer analyzer;
	auto clean_rep = analyzer.analyze(clean_parser);
	assert(clean_rep.interfaces.size() == 1);
	assert(clean_rep.interfaces[0].iface.protocol == "axi_stream");
	assert(clean_rep.interfaces[0].transactions == 1);
	assert(clean_rep.interfaces[0].stall_cycles == 1);
	assert(clean_rep.total_violations() == 0);

	// Violating AXIS: drops tvalid while tready==0, changes tdata while stalled, X on tdata while tvalid==1
	std::string bad_path = write_vcd(dir, "axis_bad.vcd", R"($timescale 1ns $end
$scope module tb $end
$var wire 1 ! aclk $end
$var wire 1 " aresetn $end
$var wire 1 # m_axis_tvalid $end
$var wire 1 $ m_axis_tready $end
$var wire 8 % m_axis_tdata $end
$upscope $end
$enddefinitions $end
#0
0!
0"
0#
0$
b00000000 %
#5
1!
#10
0!
1"
#15
1!
1#
0$
b10101010 %
#20
0!
#25
1!
b11110000 %
#30
0!
#35
1!
0#
#40
0!
#45
1!
1#
bxxxxxxxx %
)");
	verilog::vcd_parser bad_parser;
	assert(bad_parser.parse_all(bad_path, err));
	auto bad_rep = analyzer.analyze(bad_parser);
	assert(bad_rep.interfaces.size() == 1);
	bool saw_payload_chg = false;
	bool saw_valid_drop = false;
	bool saw_x_payload = false;
	for (const auto &v : bad_rep.interfaces[0].violations) {
		if (v.rule_id == "AXI_PAYLOAD_CHANGED") saw_payload_chg = true;
		if (v.rule_id == "AXI_VALID_DROPPED") saw_valid_drop = true;
		if (v.rule_id == "AXI_X_ON_PAYLOAD") saw_x_payload = true;
	}
	assert(saw_payload_chg && saw_valid_drop && saw_x_payload);
}

void test_axi_lite_and_tool_integration(const std::filesystem::path &dir)
{
	auto make_axil_vcd = [&](std::string_view fname, bool inject_orphan_bvalid) {
		std::string body = R"($timescale 1ns $end
$scope module tb $end
$var wire 1 ! aclk $end
$var wire 1 " aresetn $end
$var wire 1 a s_axi_awvalid $end
$var wire 1 b s_axi_awready $end
$var wire 4 c s_axi_awaddr $end
$var wire 1 d s_axi_wvalid $end
$var wire 1 e s_axi_wready $end
$var wire 8 f s_axi_wdata $end
$var wire 1 g s_axi_bvalid $end
$var wire 1 h s_axi_bready $end
$var wire 2 i s_axi_bresp $end
$var wire 1 j s_axi_arvalid $end
$var wire 1 k s_axi_arready $end
$var wire 4 l s_axi_araddr $end
$var wire 1 m s_axi_rvalid $end
$var wire 1 n s_axi_rready $end
$var wire 8 o s_axi_rdata $end
$var wire 2 p s_axi_rresp $end
$upscope $end
$enddefinitions $end
#0
0!
0"
0a
1b
b0000 c
0d
1e
b00000000 f
0g
1h
b00 i
0j
1k
b0000 l
0m
1n
b00000000 o
b00 p
#5
1!
#10
0!
1"
#15
1!
#20
0!
#25
1!
)";
		if (inject_orphan_bvalid) {
			body += "1g\n#30\n0!\n#35\n1!\n0g\n";
		} else {
			body += "1a\n1d\n#30\n0!\n#35\n1!\n0a\n0d\n1g\n#40\n0!\n#45\n1!\n0g\n";
		}
		return write_vcd(dir, fname, body);
	};

	std::string clean_path = make_axil_vcd("axil_clean.vcd", false);
	std::string bad_path = make_axil_vcd("axil_bad.vcd", true);

	verilog::vcd_parser p_clean;
	verilog::vcd_parser p_bad;
	std::string err;
	assert(p_clean.parse_all(clean_path, err));
	assert(p_bad.parse_all(bad_path, err));

	verilog::vcd_protocol_analyzer analyzer;
	auto rep_clean = analyzer.analyze(p_clean);
	assert(rep_clean.interfaces.size() == 1);
	assert(rep_clean.interfaces[0].iface.protocol == "axi_lite");
	assert(rep_clean.total_violations() == 0);

	auto rep_bad = analyzer.analyze(p_bad);
	assert(rep_bad.interfaces.size() == 1);
	assert(rep_bad.interfaces[0].violations.size() == 1);
	assert(rep_bad.interfaces[0].violations[0].rule_id == "AXI_ORPHAN_BVALID");

	// Verify vcd_file_info and vcd_compare surface protocol diagnostics automatically
	agentlib::tool_context ctx;
	ctx.fs_security.add_allowed_root(dir.string(), agentlib::access_type::read);
	ctx.fs_security.set_working_directory(dir.string());

	tools::vcd_file_info_validator info_val;
	nlohmann::json info_args = {{"path", bad_path}};
	assert(info_val.validate_args(info_args, ctx, err));
	std::string info_out = info_val.create_tool(info_args)->execute(ctx);
	assert(info_out.find("Detected Protocols") != std::string::npos);
	assert(info_out.find("AXI_ORPHAN_BVALID") != std::string::npos);

	tools::vcd_compare_validator cmp_val;
	nlohmann::json cmp_args = {{"path_a", clean_path}, {"path_b", bad_path}};
	assert(cmp_val.validate_args(cmp_args, ctx, err));
	std::string cmp_out = cmp_val.create_tool(cmp_args)->execute(ctx);
	assert(cmp_out.find("Protocol Violation(s) in File B Near Divergence") != std::string::npos);
	assert(cmp_out.find("AXI_ORPHAN_BVALID") != std::string::npos);

	tools::vcd_wave_validator wave_val;
	nlohmann::json wave_args = {{"path", bad_path}, {"signals", nlohmann::json::array({"s_axi_bvalid", "s_axi_bready"})}};
	assert(wave_val.validate_args(wave_args, ctx, err));
	std::string wave_out = wave_val.create_tool(wave_args)->execute(ctx);
	assert(wave_out.find("1 (!)") != std::string::npos);
	assert(wave_out.find("`(!)` Protocol Violations in Displayed Window") != std::string::npos);
}

void test_axi4_burst_wlast(const std::filesystem::path &dir)
{
	std::string bad_burst = write_vcd(dir, "axi4_bad_burst.vcd", R"($timescale 1ns $end
$scope module tb $end
$var wire 1 ! aclk $end
$var wire 1 " aresetn $end
$var wire 1 a awvalid $end
$var wire 1 b awready $end
$var wire 4 c awaddr $end
$var wire 8 q awlen $end
$var wire 1 d wvalid $end
$var wire 1 e wready $end
$var wire 8 f wdata $end
$var wire 1 r wlast $end
$var wire 1 g bvalid $end
$var wire 1 h bready $end
$var wire 2 i bresp $end
$var wire 1 j arvalid $end
$var wire 1 k arready $end
$var wire 4 l araddr $end
$var wire 8 s arlen $end
$var wire 1 m rvalid $end
$var wire 1 n rready $end
$var wire 8 o rdata $end
$var wire 2 p rresp $end
$var wire 1 t rlast $end
$upscope $end
$enddefinitions $end
#0
0!
0"
0a
1b
b0000 c
b00000010 q
0d
1e
b00000000 f
0r
0g
1h
b00 i
0j
1k
b0000 l
b00000000 s
0m
1n
b00000000 o
b00 p
0t
#5
1!
#10
0!
1"
#15
1!
#20
0!
#25
1!
1a
b00000010 q
#30
0!
#35
1!
0a
1d
0r
#40
0!
#45
1!
1r
#50
0!
#55
1!
0d
0r
)");
	verilog::vcd_parser parser;
	std::string err;
	assert(parser.parse_all(bad_burst, err));
	verilog::vcd_protocol_analyzer analyzer;
	auto rep = analyzer.analyze(parser);
	assert(rep.interfaces.size() == 1);
	assert(rep.interfaces[0].iface.protocol == "axi4");
	bool saw_wlast_mismatch = false;
	for (const auto &v : rep.interfaces[0].violations) {
		if (v.rule_id == "AXI_WLAST_MISMATCH") saw_wlast_mismatch = true;
	}
	assert(saw_wlast_mismatch);
}

} // namespace

int main()
{
	test_watchdog::setup_watchdog();
	test_watchdog::scoped_test_home guard("vcd_proto");
	std::filesystem::path dir = std::filesystem::path(std::getenv("HOME")) / "vcd_proto_work";
	std::filesystem::create_directories(dir);

	test_no_false_positives_on_non_axi(dir);
	test_axi_stream_clean_and_violations(dir);
	test_axi_lite_and_tool_integration(dir);
	test_axi4_burst_wlast(dir);

	std::cout << "PASS: test_vcd_protocol_analyzer\n";
	return 0;
}
