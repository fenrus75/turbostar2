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

	std::cout << "All call_token_extractor tests passed successfully!" << std::endl;
	return 0;
}
