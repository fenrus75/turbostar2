// Tested source file: src/semcode_backend.cpp, src/codemap_utils.cpp, src/call_token_extractor.cpp
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sys/stat.h>
#include "agentlib/tool_context.h"
#include "agentlib/tool_registry.h"
#include "codemap_utils.h"
#include "event_queue.h"
#include "fs_utils.h"
#include "lsp_manager.h"
#include "project_manager.h"
#include "semcode_backend.h"
#include "semcode_indexer.h"
#include "standard_lsp_backend.h"
#include "test_watchdog.h"

namespace fs = std::filesystem;

static void test_availability_and_discovery()
{
	std::cout << "Testing semcode discovery and availability..." << std::endl;

	std::string test_dir = fs_utils::get_project_tmp_dir() + "/test_semcode_avail";
	fs::remove_all(test_dir);
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

static void setup_test_index(const std::string &db_path)
{
	std::error_code ec;
	fs::remove(db_path, ec);
	semcode_indexer indexer(db_path);
	assert(indexer.open());

	std::string fn_json = R"raw([
  {
    "name": "target_func",
    "file_path": "main.c",
    "line_start": 7,
    "line_end": 15,
    "calls": ["callee_sub", "cleanup", "memset"]
  },
  {
    "name": "target_func",
    "file_path": "ext.c",
    "line_start": 1,
    "line_end": 3,
    "calls": []
  },
  {
    "name": "callee_sub",
    "file_path": "dep.c",
    "line_start": 1,
    "line_end": 3,
    "calls": []
  },
  {
    "name": "disambig_func",
    "file_path": "wrong_arch.c",
    "line_start": 1,
    "line_end": 1,
    "calls": []
  },
  {
    "name": "disambig_func",
    "file_path": "include/my_ops.h",
    "line_start": 1,
    "line_end": 1,
    "calls": []
  },
  {
    "name": "ambig_call",
    "file_path": "ambig_a.c",
    "line_start": 1,
    "line_end": 1,
    "calls": []
  },
  {
    "name": "ambig_call",
    "file_path": "ambig_b.c",
    "line_start": 1,
    "line_end": 1,
    "calls": []
  },
  {
    "name": "unique_header_op",
    "file_path": "include/my_ops.h",
    "line_start": 2,
    "line_end": 2,
    "calls": []
  },
  {
    "name": "selftest_func",
    "file_path": "tools/testing/selftests/selftest.c",
    "line_start": 1,
    "line_end": 1,
    "calls": []
  },
  {
    "name": "cleanup",
    "file_path": "tools/testing/selftests/bpf/xdp_synproxy.c",
    "line_start": 10,
    "line_end": 15,
    "calls": []
  },
  {
    "name": "cleanup",
    "file_path": "drivers/net/cleanup.c",
    "line_start": 20,
    "line_end": 25,
    "calls": []
  },
  {
    "name": "caller_one",
    "file_path": "main.c",
    "line_start": 20,
    "line_end": 25,
    "calls": ["target_func"]
  },
  {
    "name": "memset",
    "file_path": "arch/nios2/lib/memset.c",
    "line_start": 13,
    "line_end": 79,
    "calls": []
  }
])raw";

	std::string ty_json = R"raw([
  {
    "name": "dummy_struct",
    "file_path": "main.c",
    "line_start": 3,
    "line_end": 5,
    "kind": "struct",
    "underlying_type": ""
  },
  {
    "name": "custom_type_t",
    "file_path": "main.c",
    "line_start": 16,
    "line_end": 16,
    "kind": "typedef",
    "underlying_type": "int"
  },
  {
    "name": "type_and_typedef_t",
    "file_path": "main.c",
    "line_start": 17,
    "line_end": 17,
    "kind": "typedef",
    "underlying_type": "long"
  }
])raw";

	std::istringstream f_stream(fn_json);
	indexer.ingest_functions_stream(f_stream);
	std::istringstream t_stream(ty_json);
	indexer.ingest_types_stream(t_stream);
	assert(indexer.build_indices());
	indexer.close();
}

static void test_indexer_queries()
{
	std::cout << "Testing semcode indexer integration..." << std::endl;

	std::string test_dir = fs_utils::safe_absolute(fs_utils::get_project_tmp_dir() + "/test_semcode_idx").string();
	fs::create_directories(test_dir);
	fs::path semcode_dir = fs::path(test_dir) / ".semcode.db";
	fs::create_directories(semcode_dir);

	// Create dummy project files
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
		    << "    unique_header_op();\n"
		    << "    selftest_func();\n"
		    << "    cleanup();\n"
		    << "    return 42;\n"
		    << "}\n"
		    << "typedef int custom_type_t;\n"
		    << "typedef long type_and_typedef_t;\n";
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
		out << "void disambig_func(void);\n"
		    << "void unique_header_op(void) {}\n";
	}
	std::string selftest_dir = test_dir + "/tools/testing/selftests";
	fs::create_directories(selftest_dir);
	std::string selftest_file = selftest_dir + "/selftest.c";
	{
		std::ofstream out(selftest_file);
		out << "void selftest_func(void) {}\n";
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

	std::string db_file = (semcode_dir / "semcode_index.db").string();
	setup_test_index(db_file);

	semcode_backend backend(test_dir);
	assert(backend.get_indexer() != nullptr);
	assert(backend.get_indexer()->is_open());

	// 1. Definition query:
	// Single unique definition: callee_sub (line 8) -> dep.c
	auto defs_unique = backend.query_definition(src_file, 7, 6);
	assert(!defs_unique.empty());
	assert(defs_unique[0].path.find("dep.c") != std::string::npos);
	assert(defs_unique[0].range.start_y == 0);
	assert(defs_unique[0].range.end_y == 2);

	// Multiple definitions across files (target_func): must return empty!
	auto defs_ambig_target = backend.query_definition(src_file, 6, 6);
	assert(defs_ambig_target.empty());

	// Multiple definitions across files (cleanup): must return empty!
	auto defs_ambig_cleanup = backend.query_definition(src_file, 12, 6);
	assert(defs_ambig_cleanup.empty());

	// Type definition queried directly via query_type_definition
	auto type_defs = backend.query_type_definition(src_file, 15, 15);
	assert(!type_defs.empty());
	assert(type_defs[0].path.find("main.c") != std::string::npos);
	assert(type_defs[0].range.start_y == 15);

	// Definition query on type identifier: falls through cleanly to type lookup
	auto defs_type_fallback = backend.query_definition(src_file, 15, 15);
	assert(!defs_type_fallback.empty());
	assert(defs_type_fallback[0].path.find("main.c") != std::string::npos);
	assert(defs_type_fallback[0].range.start_y == 15);

	// Type definition with underlying type
	auto dual_defs = backend.query_type_definition(src_file, 16, 15);
	assert(!dual_defs.empty());
	assert(dual_defs[0].path.find("main.c") != std::string::npos);
	assert(dual_defs[0].range.start_y == 16);
	assert(dual_defs[0].kind == "typedef");
	assert(dual_defs[0].underlying_type == "long");

	// 2. References query via indexer callers
	auto refs = backend.query_references(src_file, 6, 6);
	assert(!refs.empty());
	assert(refs[0].path.find("main.c") != std::string::npos);
	assert(refs[0].range.start_y == 19); // 0-based index for line 20 (caller_one)

	// 3. Outgoing call hierarchy query (filters out ambiguous callees 'cleanup' and cross-arch 'memset')
	auto calls = backend.query_call_hierarchy_outgoing(src_file, 6, 6);
	assert(!calls.empty());
	assert(calls.size() == 1);
	assert(calls[0].name == "callee_sub");

	// 4. Batch call hierarchy
	auto batch = backend.query_call_hierarchy_outgoing_batch(src_file, {{6, 6}});
	assert(!batch.empty());
	assert(batch.size() == 1);
	assert(batch[0].item.name == "callee_sub");

	// 5. Workspace symbols
	auto syms = backend.query_workspace_symbols("target_func");
	assert(!syms.empty());
	assert(syms[0].name == "target_func");

	// 6. Type hierarchy
	auto types = backend.query_type_hierarchy_supertypes(src_file, 2, 8); // on dummy_struct (line 3)
	assert(!types.empty());
	assert(types[0].name == "dummy_struct");

	// 7. Hover request
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
		if (ev_opt.has_value() && ev_opt->type == event_type::lsp_hover_result) {
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	assert(ev_opt.has_value());
	assert(ev_opt->type == event_type::lsp_hover_result);
	assert(ev_opt->payload.find("dummy_struct") != std::string::npos);

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
	project_manager::get_instance().set_project_root(test_dir);
	project_manager::get_instance().set_lsp_backend_for_testing(std::make_unique<semcode_backend>(test_dir));
	auto outgoing_calls = tools::get_outgoing_calls_in_range(src_file, 7, 15, nullptr);
	assert(!outgoing_calls.empty());
	bool found_callee_sub = false;
	bool found_unique_header_op = false;
	for (const auto &call : outgoing_calls) {
		assert(call.target_name != "target_func");
		assert(call.target_name != "disambig_func");
		assert(call.target_name != "ambig_call");
		assert(call.target_name != "selftest_func");
		assert(call.target_name != "cleanup");
		if (call.target_name == "callee_sub") {
			assert(call.target_file.find("dep.c") != std::string::npos);
			found_callee_sub = true;
		}
		if (call.target_name == "unique_header_op") {
			assert(call.target_file.find("my_ops.h") != std::string::npos);
			found_unique_header_op = true;
		}
	}
	assert(found_callee_sub);
	assert(found_unique_header_op);

	// 10. Verify codemap table formatting
	agentlib::tool_context ctx;
	ctx.fs_security.set_working_directory(test_dir);
	ctx.fs_security.add_allowed_root(test_dir, agentlib::access_type::read);
	auto all_syms = tools::get_document_codemap_symbols(src_file, ctx, 1);
	auto selected = tools::select_prioritized_codemap_symbols(all_syms, 7, 15, src_file, ctx, 10);
	std::string table_md =
	    tools::format_codemap_table(src_file, selected.selected_symbols, 15, selected.total_symbols, selected.omitted_count, &ctx);
	assert(table_md.find("### Called Dependencies:") != std::string::npos);
	assert(table_md.find("`callee_sub`") != std::string::npos);
	assert(table_md.find("dep.c") != std::string::npos);
	assert(table_md.find("`unique_header_op`") != std::string::npos);
	assert(table_md.find("my_ops.h") != std::string::npos);
	assert(table_md.find("disambig_func") == std::string::npos);
	assert(table_md.find("wrong_arch.c") == std::string::npos);
	assert(table_md.find("selftest_func") == std::string::npos);
	assert(table_md.find("selftest.c") == std::string::npos);
	assert(table_md.find("ext.c") == std::string::npos);
	assert(table_md.find("ambig_call") == std::string::npos);
	assert(table_md.find("ambig_a.c") == std::string::npos);
	assert(table_md.find("ambig_b.c") == std::string::npos);
	assert(table_md.find("cleanup") == std::string::npos);
	assert(table_md.find("xdp_synproxy.c") == std::string::npos);

	fs::remove_all(test_dir);
	std::cout << "  Passed!" << std::endl;
}

static void test_call_hierarchy_instant()
{
	std::cout << "Testing instant call hierarchy outgoing via SQLite indexer..." << std::endl;

	std::string test_dir = fs_utils::get_project_tmp_dir() + "/test_semcode_instant_calls";
	fs::create_directories(test_dir);
	fs::path semcode_dir = fs::path(test_dir) / ".semcode.db";
	fs::create_directories(semcode_dir);

	std::string src_file = test_dir + "/work.c";
	{
		std::ofstream out(src_file);
		out << "#include <stdio.h>\n"
		    << "void fast_callee(void);\n"
		    << "void slow_callee(void);\n"
		    << "void do_work(void) {\n"
		    << "    fast_callee();\n"
		    << "    slow_callee();\n"
		    << "}\n";
	}

	std::string db_file = (semcode_dir / "semcode_index.db").string();
	{
		semcode_indexer indexer(db_file);
		assert(indexer.open());
		std::string fn_json = R"raw([
  {
    "name": "do_work",
    "file_path": "work.c",
    "line_start": 4,
    "line_end": 7,
    "calls": ["fast_callee", "slow_callee"]
  },
  {
    "name": "fast_callee",
    "file_path": "fast.c",
    "line_start": 1,
    "line_end": 5,
    "calls": []
  },
  {
    "name": "slow_callee",
    "file_path": "slow.c",
    "line_start": 1,
    "line_end": 5,
    "calls": []
  }
])raw";
		std::istringstream stream(fn_json);
		indexer.ingest_functions_stream(stream);
		assert(indexer.build_indices());
		indexer.close();
	}

	semcode_backend backend(test_dir);
	assert(backend.get_indexer() != nullptr);

	auto t_start = std::chrono::steady_clock::now();
	auto calls = backend.query_call_hierarchy_outgoing(src_file, 3, 6);
	auto t_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t_start).count();

	std::cout << "  Outgoing call hierarchy query completed in " << t_elapsed << "ms, returned " << calls.size() << " calls"
		  << std::endl;
	assert(t_elapsed < 20); // Instant SQLite B-tree query!
	assert(calls.size() == 2);
	bool found_fast = false;
	bool found_slow = false;
	for (const auto &c : calls) {
		if (c.name == "fast_callee") {
			found_fast = true;
		}
		if (c.name == "slow_callee") {
			found_slow = true;
		}
	}
	assert(found_fast);
	assert(found_slow);

	// Batch query across multiple positions
	auto batch_results = backend.query_call_hierarchy_outgoing_batch(src_file, {{3, 6}});
	assert(batch_results.size() == 2);

	fs::remove_all(test_dir);
	std::cout << "  Passed!" << std::endl;
}

int main()
{
	test_watchdog::setup_watchdog();

	test_availability_and_discovery();
	test_supported_files_and_sync();
	test_indexer_queries();
	test_call_hierarchy_instant();

	std::cout << "All semcode_backend unit tests passed successfully!" << std::endl;
	return 0;
}
