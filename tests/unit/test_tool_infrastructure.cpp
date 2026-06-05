#include <cassert>
#include <iostream>
#include "../../src/agentlib/single_string_tool_validator.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/agentlib/llm_types.h"

using namespace agentlib;

class mock_tool : public llm_tool
{
      public:
	explicit mock_tool(std::string str) : str_(std::move(str))
	{
	}
	bool validate_runtime(const tool_context & /*ctx*/, std::string & /*out_error*/) const override
	{
		return true;
	}
	std::string execute(tool_context & /*ctx*/) override
	{
		return str_;
	}

      private:
	std::string str_;
};

class mock_validator : public single_string_tool_validator
{
      public:
	std::string get_name() const override
	{
		return "mock";
	}
	std::string get_description() const override
	{
		return "mock desc";
	}
	std::string get_parameter_name() const override
	{
		return "param";
	}
	std::string get_parameter_description() const override
	{
		return "param desc";
	}

	bool validate_string_arg(const std::string &arg, const tool_context & /*ctx*/, std::string & /*out_error*/) const override
	{
		if (arg == "fail")
			return false;
		return true;
	}

	std::unique_ptr<llm_tool> create_tool_from_string(const std::string &arg) const override
	{
		return std::make_unique<mock_tool>(arg);
	}
};

int main()
{
	mock_validator validator;
	tool_context ctx;
	std::string error;

	nlohmann::json valid_args = {{"param", "success"}};
	nlohmann::json invalid_args = {{"param", "fail"}};

	// 1. Attempt to create tool WITHOUT validating. Must fail (return nullptr).
	auto tool1 = validator.create_tool(valid_args);
	assert(tool1 == nullptr && "Security invariant violated: tool created without validation!");

	// 2. Attempt to validate with failing args, then create tool. Must fail.
	bool valid = validator.validate_args(invalid_args, ctx, error);
	assert(!valid);
	auto tool2 = validator.create_tool(invalid_args);
	assert(tool2 == nullptr && "Security invariant violated: tool created after failed validation!");

	// 3. Attempt to validate with passing args, then create tool. Must succeed.
	valid = validator.validate_args(valid_args, ctx, error);
	assert(valid);
	auto tool3 = validator.create_tool(valid_args);
	assert(tool3 != nullptr && "Security invariant violated: valid tool not created!");

	// 4. Test normalize_tool_call with aliases
	{
		// Test Group 1: read_file -> fs_read_lines
		tool_call tc1;
		tc1.function.name = "read_file";
		tc1.function.arguments = "{\"file_path\": \"src/main.cpp\", \"start_line\": 10, \"end_line\": 20}";
		normalize_tool_call(tc1);
		assert(tc1.function.name == "fs_read_lines");
		auto parsed1 = nlohmann::json::parse(tc1.function.arguments);
		assert(parsed1["path"] == "src/main.cpp");
		assert(parsed1["start_line"] == 10);
		assert(parsed1["end_line"] == 20);

		// Test Group 1b: fs_read_file -> fs_read_lines
		tool_call tc1b;
		tc1b.function.name = "fs_read_file";
		tc1b.function.arguments = "{\"path\": \"src/main.cpp\", \"start_line\": 10, \"end_line\": 20}";
		normalize_tool_call(tc1b);
		assert(tc1b.function.name == "fs_read_lines");
		auto parsed1b = nlohmann::json::parse(tc1b.function.arguments);
		assert(parsed1b["path"] == "src/main.cpp");
		assert(parsed1b["start_line"] == 10);
		assert(parsed1b["end_line"] == 20);

		// Test Group 2: search_grep -> fs_grep_files
		tool_call tc2;
		tc2.function.name = "search_grep";
		tc2.function.arguments = "{\"query\": \"foo\", \"dir\": \"src/\"}";
		normalize_tool_call(tc2);
		assert(tc2.function.name == "fs_grep_files");
		auto parsed2 = nlohmann::json::parse(tc2.function.arguments);
		assert(parsed2["pattern"] == "foo");
		assert(parsed2["dir_path"] == "src/");

		// Test Group 3: list_dir -> fs_list_dir
		tool_call tc3;
		tc3.function.name = "list_dir";
		tc3.function.arguments = "{\"directory\": \"src/ui/\"}";
		normalize_tool_call(tc3);
		assert(tc3.function.name == "fs_list_dir");
		auto parsed3 = nlohmann::json::parse(tc3.function.arguments);
		assert(parsed3["path"] == "src/ui/");

		// Test Group 4: create_directory -> fs_mkdir
		tool_call tc4;
		tc4.function.name = "create_directory";
		tc4.function.arguments = "{\"dir_path\": \"tmp/dir\"}";
		normalize_tool_call(tc4);
		assert(tc4.function.name == "fs_mkdir");
		auto parsed4 = nlohmann::json::parse(tc4.function.arguments);
		assert(parsed4["path"] == "tmp/dir");

		// Test Group 5: run_tests -> fs_run_tests
		tool_call tc5;
		tc5.function.name = "run_tests";
		tc5.function.arguments = "{}";
		normalize_tool_call(tc5);
		assert(tc5.function.name == "fs_run_tests");

		// Test Group 6: git_diff -> git_diff_unstaged
		tool_call tc6;
		tc6.function.name = "git_diff";
		tc6.function.arguments = "{\"file_path\": \"src/utf8.cpp\"}";
		normalize_tool_call(tc6);
		assert(tc6.function.name == "git_diff_unstaged");
		auto parsed6 = nlohmann::json::parse(tc6.function.arguments);
		assert(parsed6["path"] == "src/utf8.cpp");
	}

	std::cout << "Tool infrastructure NVI invariant tests passed!\n";
	return 0;
}
