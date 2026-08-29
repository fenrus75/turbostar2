// Tested source file: src/agentlib/tool_tracer.cpp
#include "agentlib/tool_tracer.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "test_watchdog.h"

namespace fs = std::filesystem;

int main()
{
	test_watchdog::setup_watchdog();

	std::cout << "Testing tool_tracer..." << std::endl;

	auto &tracer = agentlib::tool_tracer::get_instance();
	tracer.reset();

	// Verify disabled by default
	assert(!tracer.is_enabled());

	// Tracing while disabled should not create files
	tracer.trace_tool_call("fs_read_lines", R"({"path":"src/main.cpp"})", "1: #include");
	assert(!fs::exists("toolcall.0"));

	// Enable tracer
	tracer.set_enabled(true);
	assert(tracer.is_enabled());

	// Trace tool call 0
	tracer.trace_tool_call("fs_read_lines", R"({"path":"src/main.cpp","start_line":1})", "1: #include <filesystem>");
	assert(fs::exists("toolcall.0"));

	// Verify file 0 contents
	{
		std::ifstream in("toolcall.0");
		std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		assert(content.find("\"name\": \"fs_read_lines\"") != std::string::npos);
		assert(content.find("\"path\": \"src/main.cpp\"") != std::string::npos);
		assert(content.find("\n---\n") != std::string::npos);
		assert(content.find("1: #include <filesystem>") != std::string::npos);
	}

	// Trace tool call 1 (with raw string fallback for invalid json)
	tracer.trace_tool_call("run_shell_command", "invalid_raw_json_string", "Execution Error: Invalid Json");
	assert(fs::exists("toolcall.1"));

	// Verify file 1 contents
	{
		std::ifstream in("toolcall.1");
		std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		assert(content.find("\"name\": \"run_shell_command\"") != std::string::npos);
		assert(content.find("invalid_raw_json_string") != std::string::npos);
		assert(content.find("\n---\n") != std::string::npos);
		assert(content.find("Execution Error: Invalid Json") != std::string::npos);
	}

	// Cleanup test files
	fs::remove("toolcall.0");
	fs::remove("toolcall.1");

	tracer.reset();
	std::cout << "tool_tracer tests passed successfully!" << std::endl;
	return 0;
}
