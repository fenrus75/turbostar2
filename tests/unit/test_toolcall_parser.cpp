// Tested source file: src/toolcall_watcher/toolcall_parser.cpp
#include "test_watchdog.h"
#include "toolcall_watcher/toolcall_parser.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void test_scan_directory_numeric_order()
{
	test_watchdog::scoped_test_home guard("toolcall_scan");
	std::filesystem::path dir = std::filesystem::path(std::getenv("HOME")) / "traces";
	std::filesystem::create_directories(dir);

	for (const char *name : {"toolcall.0", "toolcall.2", "toolcall.10", "toolcall.9", "toolcall.bak", "other.1"}) {
		std::ofstream out(dir / name);
		out << "placeholder";
	}

	auto entries = toolcall_watcher::toolcall_parser::scan_directory(dir);
	assert(entries.size() == 4);
	assert(entries[0].index == 0);
	assert(entries[1].index == 2);
	assert(entries[2].index == 9);
	assert(entries[3].index == 10);
}

void test_parse_content_and_strip_wrapper()
{
	std::string raw = R"({
  "arguments": {
    "files": ["a.sv", "b.sv"],
    "testbench": "tb/alu8_tb.sv"
  },
  "execute_duration_us": 4854052,
  "name": "verilog_simulate",
  "prepare_duration_us": 974,
  "total_duration_us": 4855026
}
---
<verilog_simulate_result>
### Simulation Output
- **Status**: PASS
</verilog_simulate_result>
)";

	auto parsed = toolcall_watcher::toolcall_parser::parse_content(raw);
	assert(parsed.has_value());
	assert(parsed->name == "verilog_simulate");
	assert(parsed->duration_label == "4.86s");
	assert(parsed->c_style_call == "verilog_simulate(files=[\"a.sv\", \"b.sv\"], testbench=\"tb/alu8_tb.sv\")");
	assert(parsed->body_markdown == "### Simulation Output\n- **Status**: PASS");
}

void test_partial_write_rejected_cleanly()
{
	assert(!toolcall_watcher::toolcall_parser::parse_content("").has_value());
	assert(!toolcall_watcher::toolcall_parser::parse_content("{\"name\": \"foo\"").has_value());
	assert(!toolcall_watcher::toolcall_parser::parse_content("{\"name\": }\n---\nbody").has_value());
}

void test_sanitize_esc_sequences()
{
	std::string raw = "{\n  \"arguments\": {},\n  \"name\": \"echo\",\n  \"total_duration_us\": 500\n}\n---\nhello\x1b[31mworld";
	auto parsed = toolcall_watcher::toolcall_parser::parse_content(raw);
	assert(parsed.has_value());
	assert(parsed->duration_label == "<1ms");
	assert(parsed->body_markdown.find('\x1b') == std::string::npos);
	assert(parsed->body_markdown == "hello?[31mworld");
}

} // namespace

int main()
{
	test_watchdog::setup_watchdog();
	test_scan_directory_numeric_order();
	test_parse_content_and_strip_wrapper();
	test_partial_write_rejected_cleanly();
	test_sanitize_esc_sequences();
	std::cout << "PASS: test_toolcall_parser\n";
	return 0;
}
