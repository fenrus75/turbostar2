// Tested source file: src/semcode_indexer.cpp
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include "fs_utils.h"
#include "semcode_indexer.h"
#include "test_watchdog.h"

namespace fs = std::filesystem;

static void test_indexer_ingest_and_query()
{
	std::cout << "Testing semcode_indexer ingest and query..." << std::endl;

	std::string test_dir = fs_utils::get_project_tmp_dir() + "/test_semcode_indexer";
	fs::create_directories(test_dir);
	std::string db_file = test_dir + "/test_index.db";

	std::string sample_funcs = R"raw([
  {
    "name": "pmd_none2",
    "file_path": "arch/m68k/include/asm/sun3_pgtable.h",
    "git_file_hash": "80ca185a18a19381f6565c464a509b1f398dcc23",
    "line_start": 112,
    "line_end": 112,
    "return_type": "int",
    "parameters": [
      {
        "name": "pmd",
        "type_name": "pmd_t *"
      }
    ],
    "calls": [
      "pmd_val"
    ],
    "types": [
      "pmd_t"
    ]
  },
  {
    "name": "switch_mm",
    "file_path": "arch/microblaze/include/asm/mmu_context_mm.h",
    "git_file_hash": "c2c77f70845562fae50debff93c10e92083ec0fb",
    "line_start": 114,
    "line_end": 120,
    "return_type": "void",
    "parameters": [
      {
        "name": "prev",
        "type_name": "struct mm_struct *"
      }
    ],
    "calls": [
      "get_mmu_context",
      "set_context"
    ],
    "types": [
      "mm_struct",
      "task_struct"
    ]
  }
])raw";

	std::string sample_types = R"raw([
  {
    "name": "sfunc_t",
    "file_path": "arch/m68k/sun3/prom/misc.c",
    "line_start": 50,
    "kind": "typedef",
    "types": [
      "void ()(void)"
    ]
  },
  {
    "name": "cpuinfo_mips",
    "file_path": "arch/mips/include/asm/cpu-info.h",
    "line_start": 52,
    "kind": "struct",
    "members": [
      {
        "name": "asid_cache",
        "type_name": "u64"
      }
    ],
    "types": [
      "cache_desc"
    ]
  }
])raw";

	semcode_indexer indexer(db_file);
	assert(indexer.open());

	std::istringstream fn_stream(sample_funcs);
	size_t fn_count = indexer.ingest_functions_stream(fn_stream);
	assert(fn_count == 2);

	std::istringstream ty_stream(sample_types);
	size_t ty_count = indexer.ingest_types_stream(ty_stream);
	assert(ty_count == 2);

	assert(indexer.build_indices());

	assert(indexer.set_metadata("git_head", "0123456789abcdef"));
	assert(indexer.get_metadata("git_head") == "0123456789abcdef");

	// Test function lookup
	auto res_fn = indexer.lookup_function("switch_mm");
	assert(res_fn.size() == 1);
	assert(res_fn[0].name == "switch_mm");
	assert(res_fn[0].file_path == "arch/microblaze/include/asm/mmu_context_mm.h");
	assert(res_fn[0].line_start == 114);
	assert(res_fn[0].line_end == 120);
	assert(res_fn[0].calls.size() == 2);
	assert(res_fn[0].calls[0] == "get_mmu_context");
	assert(res_fn[0].calls[1] == "set_context");
	assert(res_fn[0].types.size() == 2);
	assert(res_fn[0].types[0] == "mm_struct");
	assert(res_fn[0].types[1] == "task_struct");

	// Test function lookup in file range
	auto res_in_file = indexer.lookup_functions_in_file("arch/microblaze/include/asm/mmu_context_mm.h", 100, 150);
	assert(res_in_file.size() == 1);
	assert(res_in_file[0].name == "switch_mm");

	// Out of range lookup in file
	auto res_out_range = indexer.lookup_functions_in_file("arch/microblaze/include/asm/mmu_context_mm.h", 200, 300);
	assert(res_out_range.empty());

	// Test type lookup (typedef)
	auto res_ty = indexer.lookup_type("sfunc_t");
	assert(res_ty.size() == 1);
	assert(res_ty[0].name == "sfunc_t");
	assert(res_ty[0].kind == "typedef");
	assert(res_ty[0].underlying_type == "void ()(void)");

	// Test type lookup (struct)
	auto res_struct = indexer.lookup_type("cpuinfo_mips");
	assert(res_struct.size() == 1);
	assert(res_struct[0].name == "cpuinfo_mips");
	assert(res_struct[0].kind == "struct");

	indexer.close();
	fs::remove_all(test_dir);
	std::cout << "  Passed!" << std::endl;
}

static void test_cache_pruning()
{
	std::cout << "Testing semcode_indexer cache pruning..." << std::endl;

	std::string test_dir = fs_utils::get_project_tmp_dir() + "/test_semcode_prune";
	fs::create_directories(test_dir);

	std::string db1 = test_dir + "/semcode_commit1.db";
	std::string db2 = test_dir + "/semcode_commit2.db";
	std::string db3 = test_dir + "/semcode_commit3.db";
	std::string db4 = test_dir + "/semcode_commit4.db";
	std::string db1_journal = db1 + "-journal";

	// Create dummy files
	{
		std::ofstream(db1) << "dummy1";
		std::ofstream(db1_journal) << "journal1";
		std::ofstream(db2) << "dummy2";
		std::ofstream(db3) << "dummy3";
		std::ofstream(db4) << "dummy4";
	}

	// Set explicit timestamps: db1 < db2 < db3 < db4
	auto now = fs::file_time_type::clock::now();
	std::error_code ec;
	fs::last_write_time(db1, now - std::chrono::hours(4), ec);
	fs::last_write_time(db1_journal, now - std::chrono::hours(4), ec);
	fs::last_write_time(db2, now - std::chrono::hours(3), ec);
	fs::last_write_time(db3, now - std::chrono::hours(2), ec);
	fs::last_write_time(db4, now - std::chrono::hours(1), ec);

	// Prune to at most 2 databases (should keep db3 and db4; remove db1, db1-journal, db2)
	size_t pruned = semcode_indexer::prune_cache_directory(test_dir, 2);
	assert(pruned == 2);

	assert(!fs::exists(db1));
	assert(!fs::exists(db1_journal));
	assert(!fs::exists(db2));
	assert(fs::exists(db3));
	assert(fs::exists(db4));

	// Test touch_database on db3
	auto before_touch = fs::last_write_time(db3, ec);
	semcode_indexer::touch_database(db3);
	auto after_touch = fs::last_write_time(db3, ec);
	assert(after_touch >= before_touch);

	fs::remove_all(test_dir);
	std::cout << "  Passed!" << std::endl;
}

static void test_build_from_project()
{
	std::cout << "Testing semcode_indexer build_from_project..." << std::endl;

	std::string test_dir = fs_utils::get_project_tmp_dir() + "/test_semcode_build_proj";
	fs::create_directories(test_dir);
	std::string db_file = test_dir + "/dbs/semcode_auto.db";
	fs::create_directories(test_dir + "/dbs");

	// Create mock semcode CLI script
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
		    << "  *\"dump-functions\"*)\n"
		    << "    TARGET=$(echo \"$QUERY\" | awk '{print $2}' | tr -d \"'\\\"\")\n"
		    << "    cat << 'EOF' > \"$TARGET\"\n"
		    << "[\n"
		    << "  {\n"
		    << "    \"name\": \"kernel_init\",\n"
		    << "    \"file_path\": \"init/main.c\",\n"
		    << "    \"line_start\": 10,\n"
		    << "    \"line_end\": 25,\n"
		    << "    \"calls\": [\"setup_arch\", \"trap_init\"],\n"
		    << "    \"types\": [\"task_struct\"]\n"
		    << "  }\n"
		    << "]\n"
		    << "EOF\n"
		    << "    ;;\n"
		    << "  *\"dump-types\"*)\n"
		    << "    TARGET=$(echo \"$QUERY\" | awk '{print $2}' | tr -d \"'\\\"\")\n"
		    << "    cat << 'EOF' > \"$TARGET\"\n"
		    << "[\n"
		    << "  {\n"
		    << "    \"name\": \"task_struct\",\n"
		    << "    \"file_path\": \"include/linux/sched.h\",\n"
		    << "    \"line_start\": 500,\n"
		    << "    \"kind\": \"struct\",\n"
		    << "    \"types\": [\"thread_info\"]\n"
		    << "  }\n"
		    << "]\n"
		    << "EOF\n"
		    << "    ;;\n"
		    << "esac\n";
	}
	chmod(mock_cli.c_str(), 0755);

	semcode_indexer indexer(db_file);
	bool ok = indexer.build_from_project(test_dir, mock_cli, 2);
	assert(ok);

	// Open the generated database and verify entries
	assert(indexer.open());

	auto fns = indexer.lookup_function("kernel_init");
	assert(fns.size() == 1);
	assert(fns[0].name == "kernel_init");
	assert(fns[0].file_path == "init/main.c");
	assert(fns[0].line_start == 10);
	assert(fns[0].line_end == 25);
	assert(fns[0].calls.size() == 2);
	assert(fns[0].calls[0] == "setup_arch");
	assert(fns[0].calls[1] == "trap_init");
	assert(fns[0].types.size() == 1);
	assert(fns[0].types[0] == "task_struct");

	auto tys = indexer.lookup_type("task_struct");
	assert(tys.size() == 1);
	assert(tys[0].name == "task_struct");
	assert(tys[0].kind == "struct");

	indexer.close();
	fs::remove_all(test_dir);
	std::cout << "  Passed!" << std::endl;
}

int main()
{
	test_watchdog::setup_watchdog();
	test_indexer_ingest_and_query();
	test_cache_pruning();
	test_build_from_project();
	std::cout << "All semcode_indexer tests passed successfully!" << std::endl;
	return 0;
}
