#include "test_watchdog.h"
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/project_manager.h"

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
	test_watchdog::setup_watchdog(30);
	// Initialize tool registry and context
	tool_registry &registry = tool_registry::get_instance();

	// Test family reason and guidance registration
	assert(registry.get_tool_family_reason("my_test_family").empty());
	assert(registry.get_tool_family_guidance("my_test_family").empty());
	registry.register_tool_family("my_test_family", "reason for my test family", "guidance for my test family");
	assert(registry.get_tool_family_reason("my_test_family") == "reason for my test family");
	assert(registry.get_tool_family_guidance("my_test_family") == "guidance for my test family");
	registry.unregister_tool_family("my_test_family");
	assert(registry.get_tool_family_reason("my_test_family").empty());
	assert(registry.get_tool_family_guidance("my_test_family").empty());

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

	registry.register_tool_family("my_test_family", "reason for my test family", "guidance for my test family");
	std::string execute_result = registry.execute_tool("activate_tool_family", valid_args.dump(), ctx);
	std::cout << "Execution Result:\n" << execute_result << "\n";
	assert(execute_result.find("Tool family 'my_test_family' has been successfully activated") != std::string::npos);
	assert(execute_result.find("guidance for my test family") != std::string::npos);

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

	// 4. Test active tool family and skill persistence
	{
		std::filesystem::path temp_home = std::filesystem::absolute("./test_activate_tf_home");
		if (std::filesystem::exists(temp_home)) {
			std::filesystem::remove_all(temp_home);
		}
		std::filesystem::create_directories(temp_home);
		setenv("HOME", temp_home.c_str(), 1);

		// Initialize project_manager with the custom HOME set
		project_manager::get_instance().initialize();

		auto model = std::make_shared<ai_model>("test-model", "Test Model", "http://localhost", "Test", 0.0, 0.0);
		auto agent = ai_agent::create(2, "PersistenceAgent", model, nullptr, nullptr);
		
		agent->add_active_tool_family("my_test_family");
		agent->add_active_skill("my_test_skill");
		agent->set_planning(true, 5);
		agent->set_plan_file("docs/my_test_plan.md");
		
		agent->save_active_state();

		// Create a new agent instance with same ID/Name
		auto agent2 = ai_agent::create(2, "PersistenceAgent", model, nullptr, nullptr);
		bool restored = agent2->load_active_state(false);
		assert(restored && "Agent state should be successfully restored!");

		auto active_fams = agent2->get_active_tool_families();
		auto active_skills = agent2->get_active_skills();

		assert(std::find(active_fams.begin(), active_fams.end(), "my_test_family") != active_fams.end() && "Restored agent should have my_test_family active!");
		assert(std::find(active_skills.begin(), active_skills.end(), "my_test_skill") != active_skills.end() && "Restored agent should have my_test_skill active!");
		assert(agent2->is_planning() && "Restored agent should be in plan mode!");
		assert(agent2->get_planning_start_index() == 5 && "Restored agent planning start index should be 5!");
		assert(agent2->get_plan_file() == "docs/my_test_plan.md" && "Restored agent plan file should be docs/my_test_plan.md!");

		std::filesystem::remove_all(temp_home);
	}

	// Clean up
	registry.unregister_validator("test_tool");
	std::cout << "activate_tool_family tests passed successfully.\n";
	return 0;
}
