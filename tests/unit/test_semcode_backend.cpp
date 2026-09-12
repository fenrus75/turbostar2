// Tested source file: src/semcode_backend.cpp, src/codemap_utils.cpp, src/call_token_extractor.cpp
#include <cassert>
#include <sys/stat.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "agentlib/tool_context.h"
#include "agentlib/tool_registry.h"
#include "codemap_utils.h"
#include "event_queue.h"
#include "fs_utils.h"
#include "lsp_manager.h"
#include "project_manager.h"
#include "semcode_backend.h"
#include "standard_lsp_backend.h"
#include "test_watchdog.h"
#include <nlohmann/json.hpp>

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

	std::string test_dir = fs_utils::safe_absolute(fs_utils::get_project_tmp_dir() + "/test_semcode_cli").string();
	fs::create_directories(test_dir);

	// Create a dummy source file
	std::string src_file = test_dir + "/main.c";
	{
		std::ofstream out(src_file);
		out << "#include <my_ops.h>\n"
		    << "\n"
		    << "struct dummy_struct {\n"
		    << "    int field;\n"
		    << "};\n"
		    << "\n"
		    << "int target_func(void) {\n"
		    << "    callee_sub(123);\n"
		    << "    disambig_func();\n"
		    << "    ambig_call();\n"
		    << "    return 42;\n"
		    << "}\n";
	}
	std::string dep_file = test_dir + "/dep.c";
	{
		std::ofstream out(dep_file);
		out << "void callee_sub(int x) {\n"
		    << "    (void)x;\n"
		    << "}\n";
	}
	std::string ext_file = test_dir + "/ext.c";
	{
		std::ofstream out(ext_file);
		out << "int target_func(void) {\n"
		    << "    return 99;\n"
		    << "}\n";
	}
	std::string include_dir = test_dir + "/include";
	fs::create_directories(include_dir);
	std::string hdr_file = include_dir + "/my_ops.h";
	{
		std::ofstream out(hdr_file);
		out << "void disambig_func(void);\n";
	}
	std::string wrong_arch_file = test_dir + "/wrong_arch.c";
	{
		std::ofstream out(wrong_arch_file);
		out << "void disambig_func(void) {}\n";
	}
	std::string ambiguous_a = test_dir + "/ambig_a.c";
	{
		std::ofstream out(ambiguous_a);
		out << "void ambig_call(void) {}\n";
	}
	std::string ambiguous_b = test_dir + "/ambig_b.c";
	{
		std::ofstream out(ambiguous_b);
		out << "void ambig_call(void) {}\n";
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
		    << "    sleep 1\n"
		    << "    echo '=== Type Information ==='\n"
		    << "    printf '\\033[32mName: struct dummy_struct\\033[0m\\n'\n"
		    << "    echo 'File: main.c'\n"
		    << "    echo 'Line: 1'\n"
		    << "    echo 'Fields:'\n"
		    << "    echo '  - int field'\n"
		    << "    ;;\n"
		    << "  *\"func callee_sub\"*)\n"
		    << "    echo 'File: dep.c'\n"
		    << "    echo 'Line: 1-3'\n"
		    << "    echo 'Return type: void'\n"
		    << "    echo 'Function Definition:'\n"
		    << "    echo 'void callee_sub(int x)'\n"
		    << "    ;;\n"
		    << "  *\"func disambig_func\"*)\n"
		    << "    echo 'File: wrong_arch.c'\n"
		    << "    echo 'Line: 1-1'\n"
		    << "    echo 'Return type: void'\n"
		    << "    echo 'Function Definition:'\n"
		    << "    echo 'void disambig_func(void)'\n"
		    << "    echo ''\n"
		    << "    echo 'File: include/my_ops.h'\n"
		    << "    echo 'Line: 1-1'\n"
		    << "    echo 'Return type: void'\n"
		    << "    echo 'Function Definition:'\n"
		    << "    echo 'void disambig_func(void)'\n"
		    << "    ;;\n"
		    << "  *\"func ambig_call\"*)\n"
		    << "    echo 'File: ambig_a.c'\n"
		    << "    echo 'Line: 1-1'\n"
		    << "    echo 'Return type: void'\n"
		    << "    echo 'Function Definition:'\n"
		    << "    echo 'void ambig_call(void)'\n"
		    << "    echo ''\n"
		    << "    echo 'File: ambig_b.c'\n"
		    << "    echo 'Line: 1-1'\n"
		    << "    echo 'Return type: void'\n"
		    << "    echo 'Function Definition:'\n"
		    << "    echo 'void ambig_call(void)'\n"
		    << "    ;;\n"
		    << "  *\"func target_func\"*)\n"
		    << "    echo 'File: main.c'\n"
		    << "    echo 'Line: 5-8'\n"
		    << "    echo 'Return type: int'\n"
		    << "    echo 'Function Definition:'\n"
		    << "    echo 'int target_func(void)'\n"
		    << "    echo ''\n"
		    << "    echo 'File: ext.c'\n"
		    << "    echo 'Line: 1-3'\n"
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
	auto defs = backend.query_definition(src_file, 6, 6); // on target_func (line 7)
	assert(!defs.empty());
	assert(defs[0].path.find("main.c") != std::string::npos);
	assert(defs[0].range.start_y == 4); // 0-based index for line 5

	// 2. References query via hybrid fallback
	auto refs = backend.query_references(src_file, 6, 6);
	assert(!refs.empty());
	assert(refs[0].path.find("main.c") != std::string::npos);
	assert(refs[0].range.start_y == 19); // 0-based index for line 20

	// 3. Outgoing call hierarchy query
	auto calls = backend.query_call_hierarchy_outgoing(src_file, 6, 6);
	assert(!calls.empty());
	assert(calls[0].name == "callee_sub");
	assert(calls[0].range.start_y == 9); // 0-based index for line 10

	// 4. Batch call hierarchy
	auto batch = backend.query_call_hierarchy_outgoing_batch(src_file, {{6, 6}});
	assert(!batch.empty());
	assert(batch[0].item.name == "callee_sub");

	// 5. Workspace symbols
	auto syms = backend.query_workspace_symbols("target_func");
	assert(!syms.empty());
	assert(syms[0].name == "target_func");

	// 6. Type hierarchy
	auto types = backend.query_type_hierarchy_supertypes(src_file, 2, 8); // on dummy_struct (line 3)
	assert(!types.empty());
	assert(types[0].name == "dummy_struct");

	// 7. Hover request (must be non-blocking and asynchronous)
	event_queue queue;
	backend.start(queue);
	auto t0 = std::chrono::steady_clock::now();
	backend.request_hover(src_file, 2, 8); // on dummy_struct
	auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
	std::cout << "  request_hover returned in " << elapsed_ms << "ms" << std::endl;
	assert(elapsed_ms < 200);

	// Result arrives asynchronously on event queue
	std::optional<editor_event> ev_opt;
	for (int i = 0; i < 60; ++i) {
		ev_opt = queue.pop();
		if (ev_opt.has_value()) {
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	assert(ev_opt.has_value());
	assert(ev_opt->type == event_type::lsp_hover_result);
	assert(ev_opt->payload.find("dummy_struct") != std::string::npos);
	assert(ev_opt->payload.find("\033[") == std::string::npos);
	assert(ev_opt->payload.find("\033") == std::string::npos);

	// 8. Subsequent hover request on same location must be an instant cache hit
	auto t1 = std::chrono::steady_clock::now();
	backend.request_hover(src_file, 2, 8);
	auto cache_hit_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t1).count();
	std::cout << "  cached request_hover returned in " << cache_hit_elapsed << "ms" << std::endl;
	assert(cache_hit_elapsed < 50);
	auto cached_ev = queue.pop();
	assert(cached_ev.has_value());
	assert(cached_ev->type == event_type::lsp_hover_result);
	assert(cached_ev->payload.find("dummy_struct") != std::string::npos);

	backend.stop();

	// 9. Called dependencies test under semcode_backend:
	// Verify that get_outgoing_calls_in_range resolves callee_sub(123) in main.c lines 7-12 to dep.c,
	// resolves disambig_func() via #include <my_ops.h> tie-breaker to include/my_ops.h,
	// punts on ambig_call() (multiple candidates ambig_a.c / ambig_b.c without matching include),
	// and does NOT treat target_func definition header as an outgoing call to ext.c!
	project_manager::get_instance().set_project_root(test_dir);
	project_manager::get_instance().set_lsp_backend_for_testing(std::make_unique<semcode_backend>(test_dir));
	auto outgoing_calls = tools::get_outgoing_calls_in_range(src_file, 7, 12, nullptr);
	assert(!outgoing_calls.empty());
	bool found_callee_sub = false;
	bool found_disambig_func = false;
	for (const auto &call : outgoing_calls) {
		assert(call.target_name != "target_func");
		assert(call.target_name != "ambig_call"); // Must be punted due to unresolved cross-file ambiguity!
		if (call.target_name == "callee_sub") {
			assert(call.target_file.find("dep.c") != std::string::npos);
			found_callee_sub = true;
		}
		if (call.target_name == "disambig_func") {
			assert(call.target_file.find("my_ops.h") != std::string::npos);
			assert(call.target_file.find("wrong_arch.c") == std::string::npos);
			found_disambig_func = true;
		}
	}
	assert(found_callee_sub);
	assert(found_disambig_func);

	// 10. Verify that select_prioritized_codemap_symbols and format_codemap_table format the Called Dependencies table under semcode_backend
	agentlib::tool_context ctx;
	ctx.fs_security.set_working_directory(test_dir);
	ctx.fs_security.add_allowed_root(test_dir, agentlib::access_type::read);
	auto all_syms = tools::get_document_codemap_symbols(src_file, ctx, 1);
	auto selected = tools::select_prioritized_codemap_symbols(all_syms, 7, 12, src_file, ctx, 10);
	std::string table_md = tools::format_codemap_table(src_file, selected.selected_symbols, 12, selected.total_symbols, selected.omitted_count, &ctx);
	assert(table_md.find("### Called Dependencies:") != std::string::npos);
	assert(table_md.find("`callee_sub`") != std::string::npos);
	assert(table_md.find("dep.c") != std::string::npos);
	assert(table_md.find("`disambig_func`") != std::string::npos);
	assert(table_md.find("my_ops.h") != std::string::npos);
	assert(table_md.find("wrong_arch.c") == std::string::npos);
	assert(table_md.find("ext.c") == std::string::npos);
	assert(table_md.find("ambig_call") == std::string::npos);
	assert(table_md.find("ambig_a.c") == std::string::npos);
	assert(table_md.find("ambig_b.c") == std::string::npos);

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
