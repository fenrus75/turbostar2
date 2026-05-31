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

	// Test Offset Hint Logic
	nlohmann::json bad_args = {
	    {"path", test_file},
	    {"edits", nlohmann::json::array(
			  {{{"line_number", 1}, {"type", "replace"}, {"original_text", "Line 3"}, {"replace_with", "Fail"}}})}};

	std::string bad_result = registry.execute_tool("fs_replace_lines", bad_args.dump(), ctx);
	assert(bad_result.find("Verification Error: The block you provided is not at line 1, but it matches starting at line 3.") != std::string::npos);

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

	// Test Out of Order (Non-descending) line numbers error message
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
	assert(result_out_of_order.find("Edits MUST be sorted in strictly DESCENDING order") != std::string::npos);
	assert(result_out_of_order.find("You provided edits in this order: [5, 10, 2]") != std::string::npos);
	assert(result_out_of_order.find("Please sort your edits to target line_numbers in this order: [10, 5, 2]") != std::string::npos);

	std::remove(test_file_15.c_str());

	std::cout << "fs_replace_lines unit test passed!\n";
	return 0;
}
