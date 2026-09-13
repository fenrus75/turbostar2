// Tested source file: src/call_resolver.cpp, src/semcode_backend.cpp, src/codemap_utils.cpp
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/stat.h>

#include "agentlib/tool_context.h"
#include "call_resolver.h"
#include "codemap_utils.h"
#include "fs_utils.h"
#include "project_manager.h"
#include "semcode_backend.h"
#include "semcode_indexer.h"
#include "test_watchdog.h"

namespace fs = std::filesystem;

namespace
{

class mock_lsp_hierarchy_backend : public lsp_backend
{
      public:
	std::string expected_target_path;

	void start(event_queue &) override
	{
	}
	void stop() override
	{
	}
	void open_document(const std::string &, const std::string &) override
	{
	}
	void update_document(const std::string &, const std::string &) override
	{
	}
	void request_hover(const std::string &, int, int) override
	{
	}
	void request_document_highlight(const std::string &, int, int) override
	{
	}
	void request_selection_range(const std::string &, int, int) override
	{
	}
	[[nodiscard]] bool is_supported_file(const std::string &) const override
	{
		return true;
	}
	[[nodiscard]] std::vector<text_range> query_selection_ranges(const std::string &, int, int) override
	{
		return {};
	}
	[[nodiscard]] std::vector<location_info> query_definition(const std::string &filepath, int line, int col) override
	{
		(void)filepath;
		(void)line;
		(void)col;
		location_info loc;
		loc.path = expected_target_path;
		loc.range = {0, 0, 1, 0};
		return {loc};
	}
	[[nodiscard]] std::vector<location_info> query_type_definition(const std::string &filepath, int line, int col) override
	{
		return query_definition(filepath, line, col);
	}
	[[nodiscard]] std::vector<location_info> query_references(const std::string &, int, int) override
	{
		return {};
	}
	[[nodiscard]] std::vector<symbol_info> query_workspace_symbols(const std::string &) override
	{
		return {};
	}
	[[nodiscard]] std::vector<symbol_node> query_document_symbols(const std::string &) override
	{
		return {};
	}
	void invalidate_symbol_cache(const std::string &) override
	{
	}
	[[nodiscard]] std::vector<call_hierarchy_item> query_call_hierarchy_outgoing(const std::string &, int, int) override
	{
		return {};
	}
	[[nodiscard]] std::vector<outgoing_call_item> query_call_hierarchy_outgoing_batch(const std::string &,
											  const std::vector<std::pair<int, int>> &,
											  std::chrono::steady_clock::time_point) override
	{
		outgoing_call_item item;
		item.call_line = 1; // 0-indexed line 1 (line 2: start of __ext4_read_dirblock)
		item.item.name = "brelse";
		item.item.kind = 12; // Function
		item.item.uri = "file://" + expected_target_path;
		item.item.selection_range = {0, 0, 0, 0};
		return {item};
	}
	[[nodiscard]] std::vector<type_hierarchy_item> query_type_hierarchy_supertypes(const std::string &, int, int) override
	{
		return {};
	}
	[[nodiscard]] std::optional<std::vector<diagnostic_info>> query_file_diagnostics(const std::string &) override
	{
		return std::nullopt;
	}
	void store_file_diagnostics(const std::string &, const std::vector<diagnostic_info> &) override
	{
	}
};

} // namespace

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

	std::vector<lsp_backend::location_info> locs = {{test_dir + "/arch/mips/include/asm/ktime.h", {10, 0, 15, 0}},
							{test_dir + "/include/linux/ktime.h", {20, 0, 25, 0}},
							{test_dir + "/tools/testing/selftests/ktime.h", {30, 0, 35, 0}}};

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

	std::vector<lsp_backend::location_info> locs = {{test_dir + "/tests/unit/test_mock.cpp", {5, 0, 10, 0}}};

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
	std::cout << "Testing outgoing calls query in semcode_backend..." << std::endl;

	std::string test_dir = fs_utils::get_project_tmp_dir() + "/test_arrow_calls";
	fs::create_directories(test_dir + "/.semcode.db");

	std::string db_path = test_dir + "/.semcode.db/semcode_index.db";
	{
		semcode_indexer indexer(db_path);
		assert(indexer.open());
		std::string json_funcs = R"raw([
  {
    "name": "enter_s2idle_proper",
    "file_path": "kernel.c",
    "line_start": 1,
    "line_end": 2,
    "calls": ["WARN_ON_ONCE", "tick_freeze", "ns_to_ktime", "local_clock_noinstr"]
  },
  {
    "name": "WARN_ON_ONCE",
    "file_path": "include/warn.h",
    "line_start": 10,
    "line_end": 15,
    "calls": []
  },
  {
    "name": "tick_freeze",
    "file_path": "kernel/tick.c",
    "line_start": 20,
    "line_end": 25,
    "calls": []
  },
  {
    "name": "ns_to_ktime",
    "file_path": "include/ktime.h",
    "line_start": 30,
    "line_end": 35,
    "calls": []
  },
  {
    "name": "local_clock_noinstr",
    "file_path": "kernel/sched.c",
    "line_start": 40,
    "line_end": 45,
    "calls": []
  }
])raw";
		std::istringstream iss(json_funcs);
		indexer.ingest_functions_stream(iss);
		assert(indexer.build_indices());
		indexer.close();
	}

	setenv("SEMCODE_INDEX_DB", db_path.c_str(), 1);

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

	unsetenv("SEMCODE_INDEX_DB");
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

static void test_outgoing_calls_not_pruned_when_reading_inside_function()
{
	std::cout << "Testing outgoing calls are not pruned when range starts after function header..." << std::endl;

	std::string test_dir = fs_utils::get_project_tmp_dir() + "/test_prune_bug";
	fs::create_directories(test_dir + "/fs/ext4");
	fs::create_directories(test_dir + "/include/linux");

	std::string caller_file = test_dir + "/fs/ext4/namei.c";
	{
		std::ofstream out(caller_file);
		out << "// line 1\n"
		    << "struct buffer_head *__ext4_read_dirblock(struct inode *inode) {\n" // line 2
		    << "    int x = 0;\n"						   // line 3
		    << "    brelse(bh);\n"						   // line 4
		    << "    return NULL;\n"						   // line 5
		    << "}\n";								   // line 6
	}

	std::string hdr_file = test_dir + "/include/linux/buffer_head.h";
	{
		std::ofstream out(hdr_file);
		out << "static inline void brelse(struct buffer_head *bh) {}\n";
	}

	fs_utils::set_override_project_dir(test_dir);

	auto mock = std::make_unique<mock_lsp_hierarchy_backend>();
	mock->expected_target_path = hdr_file;
	project_manager::get_instance().set_lsp_backend_for_testing(std::move(mock));

	// Setup doc symbols where function starts at line 2 and ends at line 6
	std::vector<tools::codemap_symbol_info> doc_symbols;
	tools::codemap_symbol_info sym;
	sym.name = "__ext4_read_dirblock";
	sym.display_name = "__ext4_read_dirblock";
	sym.kind_str = "Function";
	sym.start_line = 2;
	sym.end_line = 6;
	sym.line_count = 5;
	doc_symbols.push_back(sym);

	// Read range [3, 5] (start_line 3 > function start line 2).
	// Calls of __ext4_read_dirblock must NOT be pruned!
	agentlib::tool_context ctx;
	auto calls = tools::get_outgoing_calls_in_range(caller_file, 3, 5, doc_symbols, &ctx);

	bool found_brelse = false;
	for (const auto &c : calls) {
		if (c.target_name == "brelse") {
			found_brelse = true;
			break;
		}
	}
	assert(found_brelse);

	project_manager::get_instance().set_lsp_backend_for_testing(nullptr);
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
	test_outgoing_calls_not_pruned_when_reading_inside_function();

	std::cout << "All call_resolver tests passed successfully!" << std::endl;
	return 0;
}
