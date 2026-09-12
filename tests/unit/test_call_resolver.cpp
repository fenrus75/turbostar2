// Tested source file: src/call_resolver.cpp, src/semcode_backend.cpp, src/codemap_utils.cpp
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>

#include "agentlib/tool_context.h"
#include "call_resolver.h"
#include "fs_utils.h"
#include "semcode_backend.h"
#include "test_watchdog.h"

namespace fs = std::filesystem;

static void test_disambiguation_with_included_headers()
{
	std::cout << "Testing context-aware disambiguation with included headers..." << std::endl;

	std::string test_dir = fs_utils::get_project_tmp_dir() + "/test_disambig";
	fs::create_directories(test_dir + "/drivers/cpuidle");
	fs::create_directories(test_dir + "/include/linux");
	fs::create_directories(test_dir + "/arch/mips/include/asm");
	fs::create_directories(test_dir + "/tools/testing/selftests");

	std::string caller_file = test_dir + "/drivers/cpuidle/cpuidle.c";
	{
		std::ofstream out(caller_file);
		out << "#include <linux/ktime.h>\n"
		    << "#include <linux/tick.h>\n"
		    << "\n"
		    << "void foo(void) {\n"
		    << "    ns_to_ktime(100);\n"
		    << "}\n";
	}

	std::vector<lsp_backend::location_info> locs = {
		{test_dir + "/arch/mips/include/asm/ktime.h", {10, 0, 15, 0}},
		{test_dir + "/include/linux/ktime.h", {20, 0, 25, 0}},
		{test_dir + "/tools/testing/selftests/ktime.h", {30, 0, 35, 0}}
	};

	fs_utils::set_override_project_dir(test_dir);

	auto chosen = tools::call_resolver::disambiguate_locations(locs, caller_file);
	assert(chosen.has_value());
	assert(chosen->path.find("include/linux/ktime.h") != std::string::npos);
	assert(chosen->range.start_y == 20);

	fs_utils::set_override_project_dir("");
	fs::remove_all(test_dir);
	std::cout << "  Passed!" << std::endl;
}

static void test_disambiguation_rejects_test_files_for_production()
{
	std::cout << "Testing that production caller rejects test file locations..." << std::endl;

	std::string test_dir = fs_utils::get_project_tmp_dir() + "/test_reject_tests";
	fs::create_directories(test_dir + "/src");
	fs::create_directories(test_dir + "/tests");

	std::string caller_file = test_dir + "/src/main.cpp";
	{
		std::ofstream out(caller_file);
		out << "void main_func() {}\n";
	}

	std::vector<lsp_backend::location_info> locs = {
		{test_dir + "/tests/unit/test_mock.cpp", {5, 0, 10, 0}}
	};

	fs_utils::set_override_project_dir(test_dir);

	auto chosen = tools::call_resolver::disambiguate_locations(locs, caller_file);
	assert(!chosen.has_value());

	fs_utils::set_override_project_dir("");
	fs::remove_all(test_dir);
	std::cout << "  Passed!" << std::endl;
}

static void test_extract_identifier_skips_specifiers()
{
	std::cout << "Testing extract_identifier_at skipping storage/type specifiers..." << std::endl;

	std::string test_dir = fs_utils::get_project_tmp_dir() + "/test_extract_id";
	fs::create_directories(test_dir);

	std::string src_file = test_dir + "/sample.c";
	{
		std::ofstream out(src_file);
		out << "static noinstr void enter_s2idle_proper(struct cpuidle_driver *drv,\n"
		    << "                                         int index)\n"
		    << "{\n"
		    << "    return;\n"
		    << "}\n";
	}

	// Request at column 0 (which starts with "static")
	std::string id = semcode_backend::extract_identifier_at(src_file, 0, 0);
	assert(id == "enter_s2idle_proper");

	fs::remove_all(test_dir);
	std::cout << "  Passed!" << std::endl;
}

static void test_arrow_calls_parsing()
{
	std::cout << "Testing arrow calls format parsing in semcode_backend..." << std::endl;

	std::string test_dir = fs_utils::get_project_tmp_dir() + "/test_arrow_calls";
	fs::create_directories(test_dir);

	std::string mock_cli = test_dir + "/mock_semcode";
	{
		std::ofstream out(mock_cli);
		out << "#!/bin/sh\n"
		    << "echo 'Calls: 4'\n"
		    << "echo '  → WARN_ON_ONCE'\n"
		    << "echo '  → tick_freeze'\n"
		    << "echo '  → ns_to_ktime'\n"
		    << "echo '  → local_clock_noinstr'\n";
	}
	chmod(mock_cli.c_str(), 0755);

	setenv("SEMCODE_BIN", mock_cli.c_str(), 1);

	semcode_backend backend(test_dir);

	std::string dummy_file = test_dir + "/kernel.c";
	{
		std::ofstream out(dummy_file);
		out << "void enter_s2idle_proper(void) {}\n";
	}

	auto calls = backend.query_call_hierarchy_outgoing(dummy_file, 0, 5);
	assert(calls.size() == 4);
	assert(calls[0].name == "WARN_ON_ONCE");
	assert(calls[1].name == "tick_freeze");
	assert(calls[2].name == "ns_to_ktime");
	assert(calls[3].name == "local_clock_noinstr");

	unsetenv("SEMCODE_BIN");
	fs::remove_all(test_dir);
	std::cout << "  Passed!" << std::endl;
}

static void test_resolve_target_direct_and_cross_c()
{
	std::cout << "Testing resolve_target direct URI and cross-file C call..." << std::endl;

	std::string test_dir = fs_utils::get_project_tmp_dir() + "/test_cross_c";
	fs::create_directories(test_dir + "/drivers/cpuidle");
	fs::create_directories(test_dir + "/kernel/time");

	std::string caller_file = test_dir + "/drivers/cpuidle/cpuidle.c";
	{
		std::ofstream out(caller_file);
		out << "void enter_s2idle_proper(void) {\n"
		    << "    tick_freeze();\n"
		    << "}\n";
	}

	std::string callee_file = test_dir + "/kernel/time/tick-common.c";
	{
		std::ofstream out(callee_file);
		out << "void tick_freeze(void) {\n"
		    << "    return;\n"
		    << "}\n";
	}

	fs_utils::set_override_project_dir(test_dir);

	lsp_manager::call_hierarchy_item item;
	item.name = "tick_freeze";
	item.kind = 12; // Function
	item.uri = "file://" + callee_file;
	item.selection_range = text_range{0, 0, 0, 0}; // line 1 (0-indexed 0)

	tools::outgoing_call_reference ref;
	ref.caller_file = caller_file;
	ref.call_line = 2;
	ref.target_name = "tick_freeze";

	std::unordered_map<std::string, std::vector<tools::codemap_symbol_info>> symbols_cache;
	bool resolved = tools::call_resolver::resolve_target(ref, item, symbols_cache, nullptr);

	assert(resolved);
	assert(ref.target_file == "kernel/time/tick-common.c");
	assert(ref.target_start_line == 1);
	assert(ref.target_end_line >= 1);

	fs_utils::set_override_project_dir("");
	fs::remove_all(test_dir);
	std::cout << "  Passed!" << std::endl;
}

int main()
{
	test_watchdog::setup_watchdog(30);

	test_disambiguation_with_included_headers();
	test_disambiguation_rejects_test_files_for_production();
	test_extract_identifier_skips_specifiers();
	test_arrow_calls_parsing();
	test_resolve_target_direct_and_cross_c();

	std::cout << "All call_resolver tests passed successfully!" << std::endl;
	return 0;
}
