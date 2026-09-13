// Tested source file: src/semcode_indexer.cpp
#include <cassert>
#include <filesystem>
#include <iostream>
#include <sstream>
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

int main()
{
	test_watchdog::setup_watchdog();
	test_indexer_ingest_and_query();
	std::cout << "All semcode_indexer tests passed successfully!" << std::endl;
	return 0;
}
