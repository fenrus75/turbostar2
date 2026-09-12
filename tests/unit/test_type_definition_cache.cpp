// Tested source file: src/type_definition_cache.cpp, src/type_token_extractor.cpp
#include "test_watchdog.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "fs_utils.h"
#include "lsp_backend.h"
#include "project_manager.h"
#include "type_definition_cache.h"
#include "type_token_extractor.h"

using namespace tools;

int main()
{
	test_watchdog::setup_watchdog(30);

	// ==========================================
	// Test 1: type_token_extractor Denylist & Rules
	// ==========================================
	{
		// C/C++ keywords
		assert(type_token_extractor::is_denylisted("int"));
		assert(type_token_extractor::is_denylisted("void"));
		assert(type_token_extractor::is_denylisted("class"));
		assert(type_token_extractor::is_denylisted("struct"));
		assert(type_token_extractor::is_denylisted("return"));
		assert(type_token_extractor::is_denylisted("const"));
		assert(type_token_extractor::is_denylisted("auto"));

		// Standard library types & containers
		assert(type_token_extractor::is_denylisted("string"));
		assert(type_token_extractor::is_denylisted("string_view"));
		assert(type_token_extractor::is_denylisted("vector"));
		assert(type_token_extractor::is_denylisted("map"));
		assert(type_token_extractor::is_denylisted("unique_ptr"));
		assert(type_token_extractor::is_denylisted("shared_ptr"));
		assert(type_token_extractor::is_denylisted("optional"));

		// Macros and single character identifiers
		assert(type_token_extractor::is_denylisted("T"));
		assert(type_token_extractor::is_denylisted("U"));
		assert(type_token_extractor::is_denylisted("MAX_BUFFER_SIZE"));
		assert(type_token_extractor::is_denylisted("ENABLE_FEATURE"));

		// Valid user-defined types (should NOT be denylisted)
		assert(!type_token_extractor::is_denylisted("type_definition_cache"));
		assert(!type_token_extractor::is_denylisted("tool_context"));
		assert(!type_token_extractor::is_denylisted("AgentProfile"));
		assert(!type_token_extractor::is_denylisted("WidgetController"));
		assert(!type_token_extractor::is_denylisted("token_entry_t"));
	}

	// ==========================================
	// Test 2: type_token_extractor Candidate Extraction
	// ==========================================
	{
		std::vector<std::string> lines = {"#include <memory>",
						  "#include \"widget.h\"",
						  "void process_records(const std::vector<DataRecord> &records, ControllerContext *ctx) {",
						  "    for (const auto &rec : records) {",
						  "        CustomWidget_t *widget = ctx->create_widget(rec.id);",
						  "        int count = 0;",
						  "        bool flag = true;",
						  "    }",
						  "}"};

		auto candidates = type_token_extractor::extract_candidates(lines, 1);
		std::set<std::string> candidate_names;
		for (const auto &c : candidates) {
			candidate_names.insert(c.name);
		}

		// DataRecord, ControllerContext, and CustomWidget_t should be found
		assert(candidate_names.contains("DataRecord"));
		assert(candidate_names.contains("ControllerContext"));
		assert(candidate_names.contains("CustomWidget_t"));

		// Standard keywords and names should NOT be candidates
		assert(!candidate_names.contains("vector"));
		assert(!candidate_names.contains("void"));
		assert(!candidate_names.contains("int"));
		assert(!candidate_names.contains("bool"));
		assert(!candidate_names.contains("auto"));
		assert(!candidate_names.contains("const"));
	}

	// ==========================================
	// Test 3: type_definition_cache Pre-warming & Lookup
	// ==========================================
	{
		auto &cache = type_definition_cache::get_instance();
		cache.clear();

		assert(!cache.contains("FooBar"));
		assert(!cache.lookup("FooBar").has_value());

		cache.register_resolved_type("FooBar", "class", "src/foo.h", 12, 48);
		assert(cache.contains("FooBar"));

		auto entry = cache.lookup("FooBar");
		assert(entry.has_value());
		assert(entry->state == type_cache_state::resolved);
		assert(entry->kind == "class");
		assert(entry->safe_file_path == "src/foo.h");
		assert(entry->start_line == 12);
		assert(entry->end_line == 48);
	}

	// ==========================================
	// Test 4: Table Formatting
	// ==========================================
	{
		auto &cache = type_definition_cache::get_instance();
		cache.clear();

		cache.register_resolved_type("TypeAlpha", "struct", "src/alpha.h", 10, 30);
		cache.register_resolved_type("TypeBeta", "class", "src/beta.h", 5, 25);
		cache.register_resolved_type("ktime_t", "typedef", "include/linux/types.h", 126, 126, "s64");

		auto entry_alpha = cache.lookup("TypeAlpha");
		auto entry_beta = cache.lookup("TypeBeta");
		auto entry_ktime = cache.lookup("ktime_t");
		assert(entry_alpha.has_value());
		assert(entry_beta.has_value());
		assert(entry_ktime.has_value());
		assert(entry_ktime->underlying_type == "s64");

		std::vector<type_definition_entry> entries = {*entry_alpha, *entry_beta, *entry_ktime};
		std::string table = type_definition_cache::format_type_definition_table(entries);

		assert(!table.empty());
		assert(table.find("### Type Definitions:") != std::string::npos);
		assert(table.find("`TypeAlpha`") != std::string::npos);
		assert(table.find("`TypeBeta`") != std::string::npos);
		assert(table.find("`ktime_t`") != std::string::npos);
		assert(table.find("typedef (s64)") != std::string::npos);
		assert(table.find("| `ktime_t` | typedef (s64) | `include/linux/types.h` | 126 | 126 |") != std::string::npos);
		assert(table.find("src/alpha.h") != std::string::npos);
		assert(table.find("src/beta.h") != std::string::npos);
		assert(table.find("| path | start_line | end_line |") != std::string::npos);
		assert(table.find("| 10 | 30 |") != std::string::npos);
		assert(table.find("| 5 | 25 |") != std::string::npos);
	}

	// ==========================================
	// Test 5: Cache Invalidation
	// ==========================================
	{
		auto &cache = type_definition_cache::get_instance();
		cache.clear();

		cache.register_resolved_type("TypeToInvalidate", "struct", "src/to_edit.h", 1, 10);
		cache.register_resolved_type("TypeToKeep", "class", "src/other.h", 1, 10);
		assert(cache.contains("TypeToInvalidate"));
		assert(cache.contains("TypeToKeep"));

		cache.invalidate_file("src/to_edit.h");
		assert(!cache.contains("TypeToInvalidate"));
		assert(cache.contains("TypeToKeep"));
	}

	// ==========================================
	// Test 6: Async Request without LSP (Safe Degrade)
	// ==========================================
	{
		auto &cache = type_definition_cache::get_instance();
		cache.clear();

		// Should safely degrade when LSP is not running without crashing
		cache.request_async("NonExistent", "src/foo.cpp", 1, 1);
		// Once processed by worker thread, state should become unresolved or failed
		int waited_ms = 0;
		while (waited_ms < 1000) {
			auto entry = cache.lookup("NonExistent");
			if (entry.has_value() &&
			    (entry->state == type_cache_state::failed || entry->state == type_cache_state::unresolved)) {
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
			waited_ms += 20;
		}
		auto entry = cache.lookup("NonExistent");
		assert(entry.has_value());
		assert(entry->state == type_cache_state::failed || entry->state == type_cache_state::unresolved);
	}

	// ==========================================
	// Test 7: Async Resolution with 1-Line Range (e.g. semcode)
	// ==========================================
	{
		auto &cache = type_definition_cache::get_instance();
		cache.clear();

		std::string test_dir = fs_utils::get_project_tmp_dir() + "/test_type_cache_range";
		std::filesystem::create_directories(test_dir);
		std::string target_file = test_dir + "/fsmap.c";
		{
			std::ofstream out(target_file);
			for (int i = 1; i <= 41; ++i) {
				out << "// line " << i << "\n";
			}
			out << "struct ext4_getfsmap_info {\n";
			out << "    int field1;\n";
			out << "    int field2;\n";
			out << "};\n";
		}

		class mock_single_line_lsp : public lsp_backend {
		public:
			std::string tgt_;
			std::string macro_tgt_;
			std::string typedef_tgt_;
			mock_single_line_lsp(std::string tgt, std::string macro_tgt, std::string typedef_tgt = "")
				: tgt_(std::move(tgt)), macro_tgt_(std::move(macro_tgt)), typedef_tgt_(std::move(typedef_tgt)) {}
			void start(event_queue &) override {}
			void stop() override {}
			void open_document(const std::string &, const std::string &) override {}
			void update_document(const std::string &, const std::string &) override {}
			void request_hover(const std::string &, int, int) override {}
			void request_document_highlight(const std::string &, int, int) override {}
			void request_selection_range(const std::string &, int, int) override {}
			[[nodiscard]] bool is_supported_file(const std::string &) const override { return true; }
			[[nodiscard]] std::vector<text_range> query_selection_ranges(const std::string &, int, int) override { return {}; }
			[[nodiscard]] std::vector<location_info> query_definition(const std::string &filepath, int line, int) override
			{
				location_info loc;
				if (filepath.find("my_types") != std::string::npos) {
					loc.path = typedef_tgt_;
					loc.range = {line, 0, line, 0};
				} else if (filepath.find("macro") != std::string::npos || line == 1) {
					loc.path = macro_tgt_;
					loc.range = {1, 0, 1, 0}; // Line 2 to 2 (1-line range)
				} else {
					loc.path = tgt_;
					loc.range = {41, 0, 41, 0}; // Line 42 to 42 (1-line range like semcode)
				}
				return {loc};
			}
			[[nodiscard]] std::vector<location_info> query_type_definition(const std::string &filepath, int line, int character) override
			{
				return query_definition(filepath, line, character);
			}
			[[nodiscard]] std::vector<location_info> query_references(const std::string &, int, int) override { return {}; }
			[[nodiscard]] std::vector<symbol_info> query_workspace_symbols(const std::string &) override { return {}; }
			[[nodiscard]] std::vector<symbol_node> query_document_symbols(const std::string &) override { return {}; }
			void invalidate_symbol_cache(const std::string &) override {}
			[[nodiscard]] std::vector<call_hierarchy_item> query_call_hierarchy_outgoing(const std::string &, int, int) override { return {}; }
			[[nodiscard]] std::vector<outgoing_call_item> query_call_hierarchy_outgoing_batch(
				const std::string &, const std::vector<std::pair<int, int>> &,
				std::chrono::steady_clock::time_point) override { return {}; }
			[[nodiscard]] std::vector<type_hierarchy_item> query_type_hierarchy_supertypes(const std::string &, int, int) override { return {}; }
			[[nodiscard]] std::optional<std::vector<diagnostic_info>> query_file_diagnostics(const std::string &) override { return std::nullopt; }
			void store_file_diagnostics(const std::string &, const std::vector<diagnostic_info> &) override {}
		};

		std::string macro_target = test_dir + "/macro_type.h";
		{
			std::ofstream out(macro_target);
			out << "// line 1\n";
			out << "struct __attribute__((aligned(64))) macro_wrapped_type {\n";
			out << "    int a;\n";
			out << "    int b;\n";
			out << "    char buf[16];\n";
			out << "};\n";
		}

		std::string typedef_target = test_dir + "/my_types.h";
		{
			std::ofstream out(typedef_target);
			out << "// line 1\n";
			out << "typedef s64 ktime_t;\n";
			out << "using custom_alias = uint32_t;\n";
		}

		project_manager::get_instance().set_project_root(test_dir);
		project_manager::get_instance().set_lsp_backend_for_testing(
			std::make_unique<mock_single_line_lsp>(target_file, macro_target, typedef_target));

		cache.request_async("ext4_getfsmap_info", target_file, 41, 0);
		cache.request_async("macro_wrapped_type", macro_target, 1, 0);
		cache.request_async("ktime_t", typedef_target, 1, 0);
		cache.request_async("custom_alias", typedef_target, 2, 0);

		int waited_ms = 0;
		while (waited_ms < 2000) {
			auto entry1 = cache.lookup("ext4_getfsmap_info");
			auto entry2 = cache.lookup("macro_wrapped_type");
			auto entry3 = cache.lookup("ktime_t");
			auto entry4 = cache.lookup("custom_alias");
			if (entry1.has_value() && entry1->state == type_cache_state::resolved &&
			    entry2.has_value() && entry2->state == type_cache_state::resolved &&
			    entry3.has_value() && entry3->state == type_cache_state::resolved &&
			    entry4.has_value() && entry4->state == type_cache_state::resolved) {
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
			waited_ms += 20;
		}

		auto entry = cache.lookup("ext4_getfsmap_info");
		assert(entry.has_value());
		assert(entry->state == type_cache_state::resolved);
		assert(entry->start_line == 42);
		assert(entry->end_line == 45); // Line 42 to 45! Not 42!

		auto entry_macro = cache.lookup("macro_wrapped_type");
		assert(entry_macro.has_value());
		assert(entry_macro->state == type_cache_state::resolved);
		assert(entry_macro->start_line == 2);
		assert(entry_macro->end_line == 6); // Line 2 to 6! Not 2!
		assert(entry_macro->kind == "struct");

		auto entry_ktime = cache.lookup("ktime_t");
		assert(entry_ktime.has_value());
		assert(entry_ktime->state == type_cache_state::resolved);
		assert(entry_ktime->kind == "typedef");
		assert(entry_ktime->underlying_type == "s64");
		assert(entry_ktime->start_line == 2);
		assert(entry_ktime->end_line == 2);

		auto entry_alias = cache.lookup("custom_alias");
		assert(entry_alias.has_value());
		assert(entry_alias->state == type_cache_state::resolved);
		assert(entry_alias->kind == "typedef");
		assert(entry_alias->underlying_type == "uint32_t");
		assert(entry_alias->start_line == 3);
		assert(entry_alias->end_line == 3);

		std::vector<type_definition_entry> resolved_entries = {*entry_ktime, *entry_alias};
		std::string typedef_table = type_definition_cache::format_type_definition_table(resolved_entries);
		assert(typedef_table.find("| `ktime_t` | typedef (s64) |") != std::string::npos);
		assert(typedef_table.find("| `custom_alias` | typedef (uint32_t) |") != std::string::npos);

		std::filesystem::remove_all(test_dir);
	}

	std::cout << "All type_definition_cache unit tests passed successfully!\n";
	return 0;
}
