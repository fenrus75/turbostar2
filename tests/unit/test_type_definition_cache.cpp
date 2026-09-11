// Tested source file: src/type_definition_cache.cpp, src/type_token_extractor.cpp
#include "test_watchdog.h"

#include <cassert>
#include <chrono>
#include <iostream>
#include <set>
#include <string>
#include <thread>
#include <vector>

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

		auto entry_alpha = cache.lookup("TypeAlpha");
		auto entry_beta = cache.lookup("TypeBeta");
		assert(entry_alpha.has_value());
		assert(entry_beta.has_value());

		std::vector<type_definition_entry> entries = {*entry_alpha, *entry_beta};
		std::string table = type_definition_cache::format_type_definition_table(entries);

		assert(!table.empty());
		assert(table.find("### Type Definitions:") != std::string::npos);
		assert(table.find("`TypeAlpha`") != std::string::npos);
		assert(table.find("`TypeBeta`") != std::string::npos);
		assert(table.find("src/alpha.h") != std::string::npos);
		assert(table.find("src/beta.h") != std::string::npos);
		assert(table.find("| Start | End |") != std::string::npos);
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

	std::cout << "All type_definition_cache unit tests passed successfully!\n";
	return 0;
}
