// Tested source file: src/semcode_backend.cpp
#include <cassert>
#include <sys/stat.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "event_queue.h"
#include "fs_utils.h"
#include "lsp_manager.h"
#include "project_manager.h"
#include "semcode_backend.h"
#include "standard_lsp_backend.h"
#include "test_watchdog.h"

namespace fs = std::filesystem;

static void test_availability_and_discovery()
{
	std::cout << "Testing semcode discovery and availability..." << std::endl;

	std::string test_dir = fs_utils::get_project_tmp_dir() + "/test_semcode_avail";
	fs::create_directories(test_dir);

	// In empty directory with no .semcode.db
	assert(!semcode_backend::is_available(test_dir));

	// Create .semcode.db directory
	fs::create_directories(fs::path(test_dir) / ".semcode.db");

	// Create a dummy executable script
	std::string mock_lsp = test_dir + "/mock_semcode_lsp";
	{
		std::ofstream out(mock_lsp);
		out << "#!/bin/sh\nexit 0\n";
	}
	chmod(mock_lsp.c_str(), 0755);

	// Set override
	setenv("SEMCODE_LSP_BIN", mock_lsp.c_str(), 1);
	assert(semcode_backend::find_semcode_lsp() == mock_lsp);
	assert(semcode_backend::is_available(test_dir));

	// Test lsp_manager factory instantiation
	auto backend = lsp_manager::create_default_backend(test_dir);
	assert(backend != nullptr);
	assert(dynamic_cast<semcode_backend *>(backend.get()) != nullptr);

	unsetenv("SEMCODE_LSP_BIN");
	fs::remove_all(test_dir);
	std::cout << "  Passed!" << std::endl;
}

static void test_supported_files_and_sync()
{
	std::cout << "Testing file support and document tracking..." << std::endl;

	std::string test_dir = fs_utils::get_project_tmp_dir() + "/test_semcode_files";
	fs::create_directories(test_dir);

	semcode_backend backend(test_dir);

	assert(backend.is_supported_file("kernel/sched/core.c"));
	assert(backend.is_supported_file("include/linux/fs.h"));
	assert(backend.is_supported_file("drivers/gpu/drm.cpp"));
	assert(backend.is_supported_file("rust/kernel/lib.rs"));
	assert(backend.is_supported_file("tools/zig/main.zig"));
	assert(backend.is_supported_file("scripts/checkpatch.py"));
	assert(!backend.is_supported_file("Makefile"));
	assert(!backend.is_supported_file("README.md"));

	// Document sync overrides should track versions safely
	backend.open_document("kernel/sched/core.c", "int schedule(void) { return 0; }");
	backend.update_document("kernel/sched/core.c", "int schedule(void) { return 1; }");

	// Query empty selection ranges
	auto ranges = backend.query_selection_ranges("kernel/sched/core.c", 0, 4);
	assert(ranges.empty());

	backend.stop();
	fs::remove_all(test_dir);
	std::cout << "  Passed!" << std::endl;
}

static void test_hybrid_cli_queries()
{
	std::cout << "Testing hybrid semcode CLI integration..." << std::endl;

	std::string test_dir = fs_utils::get_project_tmp_dir() + "/test_semcode_cli";
	fs::create_directories(test_dir);

	// Create a dummy source file
	std::string src_file = test_dir + "/main.c";
	{
		std::ofstream out(src_file);
		out << "struct dummy_struct {\n"
		    << "    int field;\n"
		    << "};\n"
		    << "\n"
		    << "int target_func(void) {\n"
		    << "    return 42;\n"
		    << "}\n";
	}

	// Create mock semcode CLI script that simulates semcode responses
	std::string mock_cli = test_dir + "/mock_semcode";
	{
		std::ofstream out(mock_cli);
		out << "#!/bin/sh\n"
		    << "while [ $# -gt 0 ]; do\n"
		    << "  case \"$1\" in\n"
		    << "    -q)\n"
		    << "      QUERY=\"$2\"\n"
		    << "      shift 2\n"
		    << "      ;;\n"
		    << "    *)\n"
		    << "      shift\n"
		    << "      ;;\n"
		    << "  esac\n"
		    << "done\n"
		    << "case \"$QUERY\" in\n"
		    << "  *\"type dummy_struct\"*)\n"
		    << "    echo '=== Type Information ==='\n"
		    << "    printf '\\033[32mName: struct dummy_struct\\033[0m\\n'\n"
		    << "    echo 'File: main.c'\n"
		    << "    echo 'Line: 1'\n"
		    << "    echo 'Fields:'\n"
		    << "    echo '  - int field'\n"
		    << "    ;;\n"
		    << "  *\"func target_func\"*)\n"
		    << "    echo 'File: main.c'\n"
		    << "    echo 'Line: 5-7'\n"
		    << "    echo 'Return type: int'\n"
		    << "    echo 'Function Definition:'\n"
		    << "    echo 'int target_func(void)'\n"
		    << "    ;;\n"
		    << "  *\"callers -v target_func\"*)\n"
		    << "    echo '=== Direct Callers ==='\n"
		    << "    echo '  1. caller_one'\n"
		    << "    echo '     int (main.c:20) [file SHA: abc]'\n"
		    << "    ;;\n"
		    << "  *\"calls -v target_func\"*)\n"
		    << "    echo '=== Direct Calls ==='\n"
		    << "    echo '  1. callee_sub'\n"
		    << "    echo '     void (main.c:10) [file SHA: def]'\n"
		    << "    ;;\n"
		    << "  *)\n"
		    << "    echo 'No results found'\n"
		    << "    ;;\n"
		    << "esac\n";
	}
	chmod(mock_cli.c_str(), 0755);

	setenv("SEMCODE_BIN", mock_cli.c_str(), 1);
	assert(semcode_backend::find_semcode_cli() == mock_cli);

	semcode_backend backend(test_dir);

	// 1. Definition query via hybrid fallback
	auto defs = backend.query_definition(src_file, 4, 6); // on target_func
	assert(!defs.empty());
	assert(defs[0].path.find("main.c") != std::string::npos);
	assert(defs[0].range.start_y == 4); // 0-based index for line 5

	// 2. References query via hybrid fallback
	auto refs = backend.query_references(src_file, 4, 6);
	assert(!refs.empty());
	assert(refs[0].path.find("main.c") != std::string::npos);
	assert(refs[0].range.start_y == 19); // 0-based index for line 20

	// 3. Outgoing call hierarchy query
	auto calls = backend.query_call_hierarchy_outgoing(src_file, 4, 6);
	assert(!calls.empty());
	assert(calls[0].name == "callee_sub");
	assert(calls[0].range.start_y == 9); // 0-based index for line 10

	// 4. Batch call hierarchy
	auto batch = backend.query_call_hierarchy_outgoing_batch(src_file, {{4, 6}});
	assert(!batch.empty());
	assert(batch[0].item.name == "callee_sub");

	// 5. Workspace symbols
	auto syms = backend.query_workspace_symbols("target_func");
	assert(!syms.empty());
	assert(syms[0].name == "target_func");

	// 6. Type hierarchy
	auto types = backend.query_type_hierarchy_supertypes(src_file, 0, 8); // on dummy_struct
	assert(!types.empty());
	assert(types[0].name == "dummy_struct");

	// 7. Hover request
	event_queue queue;
	backend.start(queue);
	backend.request_hover(src_file, 0, 8); // on dummy_struct
	auto ev_opt = queue.pop();
	assert(ev_opt.has_value());
	assert(ev_opt->type == event_type::lsp_hover_result);
	assert(ev_opt->payload.find("dummy_struct") != std::string::npos);
	assert(ev_opt->payload.find("\033[") == std::string::npos);
	assert(ev_opt->payload.find("\033") == std::string::npos);

	backend.stop();
	unsetenv("SEMCODE_BIN");
	fs::remove_all(test_dir);
	std::cout << "  Passed!" << std::endl;
}

int main()
{
	test_watchdog::setup_watchdog();

	test_availability_and_discovery();
	test_supported_files_and_sync();
	test_hybrid_cli_queries();

	std::cout << "All semcode_backend unit tests passed successfully!" << std::endl;
	return 0;
}
