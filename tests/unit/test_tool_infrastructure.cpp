#include <cassert>
#include <iostream>
#include "../../src/agentlib/single_string_tool_validator.h"

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

	std::cout << "Tool infrastructure NVI invariant tests passed!\n";
	return 0;
}
