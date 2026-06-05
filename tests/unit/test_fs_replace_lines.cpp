#include <cassert>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include "../../src/agentlib/tool_context.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/tools/fs_replace_lines/fs_replace_lines.h"

void create_dummy_file(const std::string &path)
{
	std::ofstream out(path);
	out << "Line 1\nLine 2\nLine 3\n";
	out.close();
}

int main()
{
	std::string test_file = "test_poem.txt";
	create_dummy_file(test_file);

	agentlib::tool_context ctx;
	ctx.fs_security.set_working_directory(std::filesystem::current_path());
	ctx.fs_security.add_allowed_root(std::filesystem::current_path(), agentlib::access_type::write);
	ctx.fs_security.add_allowed_root(std::filesystem::current_path(), agentlib::access_type::read);

	nlohmann::json args = {
	    {"path", test_file},
	    {"edits", nlohmann::json::array(
			  {{{"line_number", 2}, {"type", "replace"}, {"original_text", "Line 2"}, {"replace_with", "Replaced Line 2"}}})}};

	auto &registry = agentlib::tool_registry::get_instance();
	std::string result = registry.execute_tool("fs_replace_lines", args.dump(), ctx);

	std::cout << "Tool Output: " << result << "\n";
	assert(result.find("Successfully applied") != std::string::npos);
	assert(result.find("Code after edit for lines 1 - 3:") != std::string::npos);
	assert(result.find("1: Line 1") != std::string::npos);
	assert(result.find("2: Replaced Line 2") != std::string::npos);
	assert(result.find("3: Line 3") != std::string::npos);
	assert(result.find("- Note: Lines below this section are shifted") == std::string::npos);

	// Verify the file was changed
	std::ifstream in(test_file);
	std::string line;

	std::getline(in, line);
	assert(line == "Line 1");
	std::getline(in, line);
	assert(line == "Replaced Line 2");
	std::getline(in, line);
	assert(line == "Line 3");
	in.close();

	// Test Offset Hint Logic (within threshold <= 3, auto-shift)
	nlohmann::json bad_args = {
	    {"path", test_file},
	    {"edits", nlohmann::json::array(
			  {{{"line_number", 1}, {"type", "replace"}, {"original_text", "Replaced Line 2"}, {"replace_with", "Replaced Line 2 Shifted"}}})}};

	std::string bad_result = registry.execute_tool("fs_replace_lines", bad_args.dump(), ctx);
	std::cout << "Auto-shift result: " << bad_result << "\n";
	assert(bad_result.find("Successfully applied") != std::string::npos);
	assert(bad_result.find("System Corrections Applied:") != std::string::npos);
	assert(bad_result.find("Edit originally at line 1 was automatically shifted to line 2") != std::string::npos);

	// Test Offset Hint Logic (outside threshold > 3, should fail)
	// Create a file with 10 lines
	{
		std::ofstream out(test_file);
		for (int i = 1; i <= 10; ++i) {
			out << "Line " << i << "\n";
		}
		out.close();
	}

	nlohmann::json too_far_args = {
	    {"path", test_file},
	    {"edits", nlohmann::json::array(
			  {{{"line_number", 1}, {"type", "replace"}, {"original_text", "Line 6"}, {"replace_with", "Fail"}}})}};

	std::string too_far_result = registry.execute_tool("fs_replace_lines", too_far_args.dump(), ctx);
	std::cout << "Too far offset result: " << too_far_result << "\n";
	assert(too_far_result.find("Verification Error: The block you provided is not at line 1, but it matches starting at line 6. Please update your line_number.") != std::string::npos);

	// Test Ambiguous Multiple Matches Logic (should fail)
	{
		std::ofstream out(test_file);
		out << "Duplicate\nLine 2\nDuplicate\n";
		out.close();
	}
	nlohmann::json ambiguous_args = {
	    {"path", test_file},
	    {"edits", nlohmann::json::array(
			  {{{"line_number", 2}, {"type", "replace"}, {"original_text", "Duplicate"}, {"replace_with", "Ambiguous"}}})}};

	std::string ambiguous_result = registry.execute_tool("fs_replace_lines", ambiguous_args.dump(), ctx);
	std::cout << "Ambiguous matches result: " << ambiguous_result << "\n";
	assert(ambiguous_result.find("Verification Error: Multiple matches found for your block within +/- 25 lines (at lines [1, 3])") != std::string::npos);

	std::remove(test_file.c_str());

	// Test 15-line file, editing line 15
	std::string test_file_15 = "test_15_lines.txt";
	{
		std::ofstream out(test_file_15);
		for (int i = 1; i <= 15; ++i) {
			out << "Line " << i << "\n";
		}
		out.close();
	}

	nlohmann::json args_15 = {
	    {"path", test_file_15},
	    {"edits", nlohmann::json::array(
			  {{{"line_number", 15}, {"type", "replace"}, {"original_text", "Line 15"}, {"replace_with", "Replaced Line 15"}}})}};

	std::string result_15 = registry.execute_tool("fs_replace_lines", args_15.dump(), ctx);
	std::cout << "15-line replacement result: " << result_15 << "\n";
	assert(result_15.find("Successfully applied") != std::string::npos);

	nlohmann::json args_16_add = {
	    {"path", test_file_15},
	    {"edits", nlohmann::json::array(
			  {{{"line_number", 16}, {"type", "add"}, {"replace_with", "Line 16"}}})}};
	std::string result_16 = registry.execute_tool("fs_replace_lines", args_16_add.dump(), ctx);
	std::cout << "16-line add result: " << result_16 << "\n";
	assert(result_16.find("Successfully applied") != std::string::npos);

	nlohmann::json args_100_add = {
	    {"path", test_file_15},
	    {"edits", nlohmann::json::array(
			  {{{"line_number", 100}, {"type", "add"}, {"replace_with", "Line 100"}}})}};
	std::string result_100 = registry.execute_tool("fs_replace_lines", args_100_add.dump(), ctx);
	std::cout << "100-line add result (should fail): " << result_100 << "\n";
	assert(result_100.find("Verification Error") != std::string::npos);
	assert(result_100.find("out of bounds") != std::string::npos);
	assert(result_100.find("The file is 16 lines long.") != std::string::npos);

	// Test Out of Order (Non-descending) line numbers auto-sorting
	nlohmann::json args_out_of_order = {
	    {"path", test_file_15},
	    {"edits", nlohmann::json::array({
		{{"line_number", 5}, {"type", "add"}, {"replace_with", "Line 5"}},
		{{"line_number", 10}, {"type", "add"}, {"replace_with", "Line 10"}},
		{{"line_number", 2}, {"type", "add"}, {"replace_with", "Line 2"}}
	    })}
	};
	std::string result_out_of_order = registry.execute_tool("fs_replace_lines", args_out_of_order.dump(), ctx);
	std::cout << "Out of order result: " << result_out_of_order << "\n";
	assert(result_out_of_order.find("Successfully applied 3 edits") != std::string::npos);
	assert(result_out_of_order.find("System Corrections Applied:") != std::string::npos);
	assert(result_out_of_order.find("Edits were automatically sorted in descending order to prevent line shifting issues.") != std::string::npos);

	// Test Out of Order with verification errors (should fail and report both mismatches and correct sorting order)
	nlohmann::json args_out_of_order_fail = {
	    {"path", test_file_15},
	    {"edits", nlohmann::json::array({
		{{"line_number", 5}, {"type", "replace"}, {"original_text", "Line 99"}, {"replace_with", "Line 5 edit"}},
		{{"line_number", 10}, {"type", "replace"}, {"original_text", "Line 88"}, {"replace_with", "Line 10 edit"}},
		{{"line_number", 2}, {"type", "add"}, {"replace_with", "Line 2 edit"}}
	    })}
	};
	std::string result_out_of_order_fail = registry.execute_tool("fs_replace_lines", args_out_of_order_fail.dump(), ctx);
	std::cout << "Out of order fail result:\n" << result_out_of_order_fail << "\n";
	assert(result_out_of_order_fail.find("Verification Error at line 5.") != std::string::npos);
	assert(result_out_of_order_fail.find("Verification Error at line 10.") != std::string::npos);
	assert(result_out_of_order_fail.find("Error: Edits MUST be sorted in strictly DESCENDING order") != std::string::npos);
	assert(result_out_of_order_fail.find("You provided edits in this order: [5, 10, 2]") != std::string::npos);
	assert(result_out_of_order_fail.find("Please sort your edits to target line_numbers in this order: [10, 5, 2]") != std::string::npos);

	// Test Multiple Verification Errors (All mismatches in one go)
	nlohmann::json args_multiple_errors = {
	    {"path", test_file_15},
	    {"edits", nlohmann::json::array({
		{{"line_number", 12}, {"type", "replace"}, {"original_text", "Line 99"}, {"replace_with", "Line 12 edited"}},
		{{"line_number", 4}, {"type", "replace"}, {"original_text", "Line 88"}, {"replace_with", "Line 4 edited"}}
	    })}
	};
	std::string result_multiple_errors = registry.execute_tool("fs_replace_lines", args_multiple_errors.dump(), ctx);
	std::cout << "Multiple errors result:\n" << result_multiple_errors << "\n";
	assert(result_multiple_errors.find("Verification Error at line 12.") != std::string::npos);
	assert(result_multiple_errors.find("Verification Error at line 4.") != std::string::npos);

	// Test Drift Warning Logic (drift >= 15)
	// Reset the drift tracker for test_file_15 by reading it first
	{
		nlohmann::json read_args = {{"path", test_file_15}, {"start_line", 1}, {"end_line", 15}};
		registry.execute_tool("fs_read_lines", read_args.dump(), ctx);
	}

	// Now apply an edit that shifts by 15 lines (e.g. inserting 15 lines)
	nlohmann::json args_drift_15 = {
	    {"path", test_file_15},
	    {"edits", nlohmann::json::array({
		{{"line_number", 5}, {"type", "add"}, {"replace_with", "Add 1\nAdd 2\nAdd 3\nAdd 4\nAdd 5\nAdd 6\nAdd 7\nAdd 8\nAdd 9\nAdd 10\nAdd 11\nAdd 12\nAdd 13\nAdd 14\nAdd 15"}}
	    })}
	};
	std::string result_drift_15 = registry.execute_tool("fs_replace_lines", args_drift_15.dump(), ctx);
	std::cout << "Drift >= 15 result:\n" << result_drift_15 << "\n";
	assert(result_drift_15.find("Warning: File has drifted by ") != std::string::npos);
	assert(result_drift_15.find("Before making further edits, we recommend refreshing your view with fs_read_lines.") != std::string::npos);
	assert(result_drift_15.find("- Note: Lines below this section are shifted") == std::string::npos);

	std::remove(test_file_15.c_str());

	// Test Whitespace/Indentation Mismatch Verification
	{
		std::ofstream out(test_file);
		out << "    if (x) {\n        // comment\n    }\n";
		out.close();
	}

	nlohmann::json indent_args = {
	    {"path", test_file},
	    {"edits", nlohmann::json::array({
		{{"line_number", 1}, {"type", "replace"}, {"original_text", "if (x) {"}, {"replace_with", "    if (y) {"}}
	    })}
	};

	std::string indent_result = registry.execute_tool("fs_replace_lines", indent_args.dump(), ctx);
	std::cout << "Indentation mismatch result: " << indent_result << "\n";
	assert(indent_result.find("Successfully applied") != std::string::npos);

	std::remove(test_file.c_str());

	// Test Optional Type Defaults to Replace
	{
		std::ofstream out(test_file);
		out << "Line A\nLine B\nLine C\n";
		out.close();
	}

	nlohmann::json optional_type_args = {
	    {"path", test_file},
	    {"edits", nlohmann::json::array({
		{{"line_number", 2}, {"original_text", "Line B"}, {"replace_with", "Replaced Line B"}}
	    })}
	};

	std::string optional_type_result = registry.execute_tool("fs_replace_lines", optional_type_args.dump(), ctx);
	std::cout << "Optional type result: " << optional_type_result << "\n";
	assert(optional_type_result.find("Successfully applied") != std::string::npos);

	{
		std::ifstream in(test_file);
		std::string line;
		std::getline(in, line);
		assert(line == "Line A");
		std::getline(in, line);
		assert(line == "Replaced Line B");
		std::getline(in, line);
		assert(line == "Line C");
		in.close();
	}

	std::remove(test_file.c_str());

	std::cout << "fs_replace_lines unit test passed!\n";
	return 0;
}
