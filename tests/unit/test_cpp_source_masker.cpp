// Tested source file: src/languages/cpp/source_masker.cpp
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include "languages/cpp/source_masker.h"
#include "test_watchdog.h"

using namespace languages::cpp;

int main()
{
	test_watchdog::setup_watchdog(30);

	// ==========================================
	// Test 1: Single-line line comment (//)
	// ==========================================
	{
		mask_state state;
		std::string line = "int x = 42; // comment with { braces } and \"strings\"";
		std::string masked = source_masker::mask_line(line, state);
		assert(masked.size() == line.size());
		assert(masked.substr(0, 12) == "int x = 42; ");
		// Rest of the line should be spaces
		for (size_t i = 12; i < masked.size(); ++i) {
			assert(masked[i] == ' ');
		}
		assert(!state.in_block_comment);
		assert(!state.in_raw_string);
	}

	// ==========================================
	// Test 2: Inline block comment (/* ... */)
	// ==========================================
	{
		mask_state state;
		std::string line = "void foo(/* MyParam_t */ int x);";
		std::string masked = source_masker::mask_line(line, state);
		assert(masked.size() == line.size());
		assert(masked == "void foo(                int x);");
		assert(!state.in_block_comment);
	}

	// ==========================================
	// Test 3: Multi-line block comment
	// ==========================================
	{
		mask_state state;
		std::string l1 = "int a = 1; /* start comment";
		std::string l2 = "   middle with { and } and foo()";
		std::string l3 = "   end of comment */ int b = 2;";

		std::string m1 = source_masker::mask_line(l1, state);
		assert(m1.size() == l1.size());
		assert(m1.substr(0, 11) == "int a = 1; ");
		for (size_t i = 11; i < m1.size(); ++i) {
			assert(m1[i] == ' ');
		}
		assert(state.in_block_comment);

		std::string m2 = source_masker::mask_line(l2, state);
		assert(m2.size() == l2.size());
		for (char c : m2) {
			assert(c == ' ');
		}
		assert(state.in_block_comment);

		std::string m3 = source_masker::mask_line(l3, state);
		assert(m3.size() == l3.size());
		// Characters up to '*/' should be spaces, remainder is " int b = 2;"
		assert(m3.substr(20) == " int b = 2;");
		for (size_t i = 0; i < 20; ++i) {
			assert(m3[i] == ' ');
		}
		assert(!state.in_block_comment);
	}

	// ==========================================
	// Test 4: Standard string literal with escaped quotes
	// ==========================================
	{
		mask_state state;
		std::string line = "const char *msg = \"hello \\\"{world}\\\" // not a comment\"; int c = 5;";
		std::string masked = source_masker::mask_line(line, state);
		assert(masked.size() == line.size());
		assert(masked.starts_with("const char *msg = "));
		assert(masked.ends_with("; int c = 5;"));
		// No braces or slashes in masked string
		assert(masked.find('{') == std::string::npos);
		assert(masked.find('}') == std::string::npos);
		assert(masked.find('/') == std::string::npos);
		// Semicolon after msg assignment is preserved
		assert(masked[54] == ';');
		assert(masked.find("; int c = 5;") != std::string::npos);
	}

	// ==========================================
	// Test 5: Character literals with escaped quotes
	// ==========================================
	{
		mask_state state;
		std::string line = "char a = '{'; char b = '\\''; char c = '}';";
		std::string masked = source_masker::mask_line(line, state);
		assert(masked.size() == line.size());
		// No braces or single quotes in masked code
		assert(masked.find('{') == std::string::npos);
		assert(masked.find('}') == std::string::npos);
		assert(masked.find('\'') == std::string::npos);
		// Variable names and semicolons are preserved
		assert(masked.find("char a = ") != std::string::npos);
		assert(masked.find("char b = ") != std::string::npos);
		assert(masked.find("char c = ") != std::string::npos);
	}

	// ==========================================
	// Test 6: Raw string literals (single and multi-line)
	// ==========================================
	{
		// Single-line raw string
		mask_state state;
		std::string line = "const char *sql = R\"sql(SELECT * FROM t WHERE a = \"hello\" AND b = '{');)sql\"; int val = 1;";
		std::string masked = source_masker::mask_line(line, state);
		assert(masked.size() == line.size());
		assert(masked.starts_with("const char *sql = "));
		assert(masked.ends_with("; int val = 1;"));
		assert(masked.find('{') == std::string::npos);
		assert(masked.find("SELECT") == std::string::npos);
		assert(!state.in_raw_string);

		// Multi-line raw string with custom delimiter
		std::string r1 = "auto text = R\"custom(first line with \"quotes\"";
		std::string r2 = "  second line with /* comments */ and { braces }";
		std::string r3 = ")custom\"; int code = 100;";

		std::string mr1 = source_masker::mask_line(r1, state);
		assert(mr1.size() == r1.size());
		assert(mr1.starts_with("auto text = "));
		assert(state.in_raw_string);
		assert(state.raw_string_delim == "custom");

		std::string mr2 = source_masker::mask_line(r2, state);
		assert(mr2.size() == r2.size());
		for (char c : mr2) {
			assert(c == ' ');
		}
		assert(state.in_raw_string);

		std::string mr3 = source_masker::mask_line(r3, state);
		assert(mr3.size() == r3.size());
		assert(!state.in_raw_string);
		assert(mr3.ends_with("; int code = 100;"));
	}

	// ==========================================
	// Test 7: URL inside string literal
	// ==========================================
	{
		mask_state state;
		std::string line = "std::string url = \"https://turbostar.org/api\"; // comment";
		std::string masked = source_masker::mask_line(line, state);
		assert(masked.size() == line.size());
		assert(masked.starts_with("std::string url = "));
		assert(masked.find("https") == std::string::npos);
		assert(masked.find("turbostar") == std::string::npos);
		assert(masked.find("comment") == std::string::npos);
		// Semicolon between string literal and comment is preserved
		assert(masked.find(';') != std::string::npos);
	}

	// ==========================================
	// Test 8: Batch masking (mask_lines)
	// ==========================================
	{
		std::vector<std::string> doc = {
			"// Header comment",
			"void process(int x) /* inline */ {",
			"    std::string s = \"hello {world}\";",
			"    // comment with }",
			"    return;",
			"}"
		};

		auto masked_doc = source_masker::mask_lines(doc);
		assert(masked_doc.size() == doc.size());
		for (size_t i = 0; i < doc.size(); ++i) {
			assert(masked_doc[i].size() == doc[i].size());
		}

		// Line 0 is completely blank/spaces
		for (char c : masked_doc[0]) {
			assert(c == ' ');
		}

		// Line 1 has process, int x, and {
		assert(masked_doc[1].find("void process(int x)") != std::string::npos);
		assert(masked_doc[1].find('{') != std::string::npos);
		assert(masked_doc[1].find("/* inline */") == std::string::npos);

		// Line 2 has std::string s =, but "{world}" is spaces
		assert(masked_doc[2].find("std::string s = ") != std::string::npos);
		assert(masked_doc[2].find('{') == std::string::npos);
		assert(masked_doc[2].find('}') == std::string::npos);

		// Line 3 has comment with }, should be all spaces
		for (char c : masked_doc[3]) {
			assert(c == ' ');
		}

		// Line 4 has return;
		assert(masked_doc[4].find("return;") != std::string::npos);

		// Line 5 has }
		assert(masked_doc[5].find('}') != std::string::npos);
	}

	// ==========================================
	// Test 9: Multi-byte UTF-8 in comments & literals (byte invariance)
	// ==========================================
	{
		mask_state state;
		// 4-byte emoji (🎉) and 2-byte char (ä) inside comment and string
		std::string line = "int x = 1; /* 🎉 emoji */ int y = 2; // ä accent";
		std::string masked = source_masker::mask_line(line, state);
		// Exact byte length invariant
		assert(masked.size() == line.size());
		// Byte offset of downstream identifier 'y' is 100% preserved
		assert(masked.find("int y = 2;") == line.find("int y = 2;"));
		assert(masked.substr(0, 11) == "int x = 1; ");
	}

	std::cout << "All source_masker unit tests passed successfully!" << std::endl;
	return 0;
}
