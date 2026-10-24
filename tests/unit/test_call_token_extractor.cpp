// Tested source file: src/call_token_extractor.cpp
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include "call_token_extractor.h"
#include "test_watchdog.h"

int main()
{
	test_watchdog::setup_watchdog();

	std::cout << "Testing call_token_extractor extraction..." << std::endl;
	std::vector<std::string> lines = {
		"// Comment: ignore_this(123)",
		"#include <header.h>",
		"if (condition) {",
		"    callee_sub(123);",
		"    ns::process_item(foo);",
		"    obj->update_state(42);",
		"    while (retry()) {",
		"        callee_sub(456); // duplicate name, should be deduped",
		"    }",
		"    for (int i = 0; i < 10; ++i) {",
		"        sizeof(int);",
		"        static_cast<int>(val);",
		"        return a(1);",
		"    }",
		"}"
	};

	auto candidates = tools::call_token_extractor::extract_candidates("test.cpp", lines, 10);
	// Expected extracted names in order of first appearance:
	// 1. callee_sub (line 13)
	// 2. process_item (line 14)
	// 3. update_state (line 15)
	// 4. retry (line 16)
	// Notice 'if', 'while', 'for', 'sizeof', 'static_cast', 'return', 'a' are excluded.
	// Notice 'ignore_this' (L10) is excluded because it's in a comment.
	// Notice second 'callee_sub' (L17) is excluded because it is a duplicate.
	assert(candidates.size() == 4);

	assert(candidates[0].name == "callee_sub");
	assert(candidates[0].line == 13);
	assert(candidates[0].col == 4);

	assert(candidates[1].name == "process_item");
	assert(candidates[1].line == 14);

	assert(candidates[2].name == "update_state");
	assert(candidates[2].line == 15);

	assert(candidates[3].name == "retry");
	assert(candidates[3].line == 16);

	// Non-C++ file path should return empty
	auto non_cpp_candidates = tools::call_token_extractor::extract_candidates("test.txt", lines, 10);
	assert(non_cpp_candidates.empty());

	// Verify calls inside comments and string literals are ignored
	{
		std::vector<std::string> comment_and_string_lines = {
			"    real_call(1); // trailing_fake(2)",
			"    std::string s = \"string_fake(3)\";",
			"    /* multi-line comment",
			"       block_fake(4)",
			"    */",
			"    another_real_call(5);"
		};
		auto result = tools::call_token_extractor::extract_candidates("test.cpp", comment_and_string_lines, 1);
		assert(result.size() == 2);
		assert(result[0].name == "real_call");
		assert(result[1].name == "another_real_call");

		// Coroutine and modern C++ keywords must not be treated as calls
		std::vector<std::string> kw_call_lines = {
			"    co_await(fetch_data());",
			"    alignas(64) int buffer[16];",
			"    real_task(100);"
		};
		auto kw_result = tools::call_token_extractor::extract_candidates("test.cpp", kw_call_lines, 1);
		assert(kw_result.size() == 2);
		assert(kw_result[0].name == "fetch_data");
		assert(kw_result[1].name == "real_task");

		// Test multi-line call and comment-separated call
		std::vector<std::string> multiline_call_lines = {
			"    multiline_func",
			"        (arg1, arg2);",
			"    comment_separated /* inline */ (123);"
		};
		auto multi_result = tools::call_token_extractor::extract_candidates("test.cpp", multiline_call_lines, 1);
		assert(multi_result.size() == 2);
		assert(multi_result[0].name == "multiline_func");
		assert(multi_result[0].line == 1);
		assert(multi_result[1].name == "comment_separated");
		assert(multi_result[1].line == 3);
	}

	std::cout << "All call_token_extractor tests passed successfully!" << std::endl;
	return 0;
}
