#include "test_watchdog.h"
#include "agentlib/ai_agent.h"
#include "agentlib/command_registry.h"
#include "project_manager.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace agentlib;

int main()
{
	test_watchdog::setup_watchdog(30);
	test_watchdog::init_singletons();
	project_manager::get_instance().initialize();

	auto model = std::make_shared<ai_model>("test-model", "Test Model", "http://localhost", "Test", 0.0, 0.0);
	auto agent = ai_agent::create(1, "TestAgent", model, nullptr, nullptr);

	agent->inject_context("system", "# System Prompt Header\n\nYou are a helpful TurboStar assistant.");

	command_registry &registry = command_registry::get_instance();

	// Test 1: Dump to default filename (system_prompt_dump.md)
	{
		std::filesystem::path dump_file = std::filesystem::path(project_manager::get_instance().get_project_root()) / "system_prompt_dump.md";
		std::filesystem::remove(dump_file);

		agent_command::context ctx;
		ctx.agent = agent.get();
		ctx.arguments = "";

		auto cmd = registry.get_command("sysprompt");
		assert(cmd != nullptr);
		cmd->execute(ctx);

		assert(std::filesystem::exists(dump_file) && "Default dump file should be created");
		std::ifstream in(dump_file);
		std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		assert(content.find("# System Prompt Header") != std::string::npos);
		std::filesystem::remove(dump_file);
	}

	// Test 2: Dump to custom filename
	{
		std::filesystem::path custom_file = std::filesystem::path(project_manager::get_instance().get_project_root()) / "custom_sysprompt.md";
		std::filesystem::remove(custom_file);

		agent_command::context ctx;
		ctx.agent = agent.get();
		ctx.arguments = "custom_sysprompt.md";

		auto cmd = registry.get_command("sysprompt");
		assert(cmd != nullptr);
		cmd->execute(ctx);

		assert(std::filesystem::exists(custom_file) && "Custom dump file should be created");
		std::ifstream in(custom_file);
		std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		assert(content.find("# System Prompt Header") != std::string::npos);
		std::filesystem::remove(custom_file);
	}

	std::cout << "sysprompt command unit tests passed successfully!\n";
	return 0;
}
