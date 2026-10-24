// Tested source file: src/type_token_extractor.cpp
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include "test_watchdog.h"
#include "type_token_extractor.h"

int main()
{
	test_watchdog::setup_watchdog();

	std::cout << "Testing type_token_extractor extraction..." << std::endl;
	std::vector<std::string> lines = {
		"// Comment: class IgnoreCommentClass",
		"#include <header.h>",
		"struct RealType {",
		"    Widget widget;",
		"    const AppContext &ctx;",
		"    std::vector<ItemData> items;",
		"    std::string fake = \"struct FakeStructInString { int x; };\";",
		"    int count; // struct FakeTrailingComment",
		"    ptrdiff_t offset;",
		"    uintptr_t raw_addr;",
		"    /* multi-line comment",
		"       class FakeInBlockComment",
		"    */",
		"    FinalType final_val;",
		"};"
	};

	auto candidates = tools::type_token_extractor::extract_candidates("test.cpp", lines, 1);
	// Expected extracted types:
	// RealType, Widget, AppContext, ItemData, FinalType
	// Ignored: IgnoreCommentClass, FakeStructInString, FakeTrailingComment, FakeInBlockComment
	std::vector<std::string> names;
	for (const auto &c : candidates) {
		names.push_back(c.name);
	}

	assert(names.size() == 5);
	assert(names[0] == "RealType");
	assert(names[1] == "Widget");
	assert(names[2] == "AppContext");
	assert(names[3] == "ItemData");
	assert(names[4] == "FinalType");

	// Test multi-line type and template declarations
	std::vector<std::string> multiline_type_lines = {
		"    std::unique_ptr<",
		"        ParsedNode",
		"    > node_ptr;",
		"    CustomManager",
		"        *manager_inst;"
	};
	auto multi_candidates = tools::type_token_extractor::extract_candidates("test.cpp", multiline_type_lines, 1);
	assert(multi_candidates.size() == 2);
	assert(multi_candidates[0].name == "ParsedNode");
	assert(multi_candidates[0].line == 2);
	assert(multi_candidates[1].name == "CustomManager");
	assert(multi_candidates[1].line == 4);

	// Non-C++ file path should return empty
	auto non_cpp = tools::type_token_extractor::extract_candidates("test.txt", lines, 1);
	assert(non_cpp.empty());

	// Additional pattern coverage: const template args, pointers/refs
	{
		std::vector<std::string> template_lines = {
			"std::vector<const CustomAlpha*> alpha_list;",
			"std::unique_ptr<CustomBeta&> beta_ref;",
			"CustomGamma const* gamma_ptr;"
		};
		auto pat_res = tools::type_token_extractor::extract_candidates("test.cpp", template_lines, 1);
		bool found_alpha = false;
		bool found_beta = false;
		bool found_gamma = false;
		for (const auto &tok : pat_res) {
			if (tok.name == "CustomAlpha") found_alpha = true;
			if (tok.name == "CustomBeta") found_beta = true;
			if (tok.name == "CustomGamma") found_gamma = true;
		}
		assert(found_alpha);
		assert(found_beta);
		assert(found_gamma);
	}

	// Test 15 limit caps across various patterns
	{
		std::vector<std::string> cap_lines;
		for (int i = 0; i < 25; ++i) {
			cap_lines.push_back("class CapType" + std::to_string(i) + " {};");
		}
		auto cap_res = tools::type_token_extractor::extract_candidates("test.cpp", cap_lines, 1);
		assert(cap_res.size() == 15);
	}
	{
		std::vector<std::string> cap_lines;
		for (int i = 0; i < 25; ++i) {
			cap_lines.push_back("CapScope" + std::to_string(i) + "::method();");
		}
		auto cap_res = tools::type_token_extractor::extract_candidates("test.cpp", cap_lines, 1);
		assert(cap_res.size() == 15);
	}
	{
		std::vector<std::string> cap_lines;
		for (int i = 0; i < 25; ++i) {
			cap_lines.push_back("std::vector<TplCap" + std::to_string(i) + "> list" + std::to_string(i) + ";");
		}
		auto cap_res = tools::type_token_extractor::extract_candidates("test.cpp", cap_lines, 1);
		assert(cap_res.size() == 15);
	}
	{
		std::vector<std::string> cap_lines;
		for (int i = 0; i < 25; ++i) {
			cap_lines.push_back("PtrCap" + std::to_string(i) + "* var" + std::to_string(i) + ";");
		}
		auto cap_res = tools::type_token_extractor::extract_candidates("test.cpp", cap_lines, 1);
		assert(cap_res.size() == 15);
	}
	{
		std::vector<std::string> cap_lines;
		for (int i = 0; i < 25; ++i) {
			cap_lines.push_back("DeclCap" + std::to_string(i) + " var" + std::to_string(i) + ";");
		}
		auto cap_res = tools::type_token_extractor::extract_candidates("test.cpp", cap_lines, 1);
		assert(cap_res.size() == 15);
	}

	std::cout << "All type_token_extractor tests passed successfully!" << std::endl;
	return 0;
}
