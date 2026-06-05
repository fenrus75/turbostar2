#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"

using namespace agentlib;

// A mock validator with a custom family name so we have at least one custom family registered.
class test_tool_validator : public tool_validator
{
      public:
	std::string get_name() const override
	{
		return "test_tool";
	}
	std::string get_description() const override
	{
		return "test tool description";
	}
	std::string get_family() const override
	{
		return "my_test_family";
	}
	nlohmann::json get_parameters_schema() const override
	{
		return {{"type", "object"}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &, const tool_context &, std::string &) const override
	{
		return true;
	}
	std::unique_ptr<llm_tool> create_tool_impl(const nlohmann::json &) const override
	{
		return nullptr;
	}
};

int main()
{
	// Initialize tool registry and context
	tool_registry &registry = tool_registry::get_instance();

	// Register our mock tool so "my_test_family" is registered
	registry.register_validator([]() { return std::make_unique<test_tool_validator>(); });

	tool_context ctx;
	ctx.fs_security.set_working_directory(std::filesystem::current_path());

	// 1. Test basic valid tool execution validation
	nlohmann::json valid_args = {{"name", "my_test_family"}};
	auto prep = registry.prepare_tool("activate_tool_family", valid_args.dump(), ctx);
	assert(prep.tool != nullptr && "Valid tool family activation should pass validation!");
	assert(prep.error_message.empty());

	// 2. Test execution of tool updates active families
	bool family_activated = false;
	ctx.is_family_active = [&](const std::string &fam) {
		if (fam == "my_test_family") {
			family_activated = true;
		}
		return true;
	};

	std::string execute_result = registry.execute_tool("activate_tool_family", valid_args.dump(), ctx);
	std::cout << "Execution Result:\n" << execute_result << "\n";
	assert(execute_result.find("Tool family 'my_test_family' has been successfully activated") != std::string::npos);

	// 3. Test validation of malicious/malformed inputs
	// A. Missing required fields
	prep = registry.prepare_tool("activate_tool_family", "{}", ctx);
	assert(prep.tool == nullptr);
	assert(!prep.error_message.empty());

	// B. Invalid type (integer instead of string)
	prep = registry.prepare_tool("activate_tool_family", "{\"name\": 123}", ctx);
	assert(prep.tool == nullptr);
	assert(!prep.error_message.empty());

	// C. Empty string
	prep = registry.prepare_tool("activate_tool_family", "{\"name\": \"\"}", ctx);
	assert(prep.tool == nullptr);
	assert(!prep.error_message.empty());
	assert(prep.error_message.find("empty") != std::string::npos);

	// D. Non-existent family name
	prep = registry.prepare_tool("activate_tool_family", "{\"name\": \"non_existent_family_name\"}", ctx);
	assert(prep.tool == nullptr);
	assert(!prep.error_message.empty());
	assert(prep.error_message.find("not found") != std::string::npos);

	// Clean up
	registry.unregister_validator("test_tool");
	std::cout << "activate_tool_family tests passed successfully.\n";
	return 0;
}
