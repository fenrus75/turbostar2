#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/skill_manager.h"
#include "../../src/agentlib/tool_registry.h"

using namespace agentlib;

void write_file(const std::filesystem::path &path, const std::string &content)
{
	std::filesystem::create_directories(path.parent_path());
	std::ofstream out(path);
	out << content;
}

int main()
{
	// 1. Create a temporary home directory
	std::filesystem::path temp_home = std::filesystem::absolute("./test_list_skills_home");
	if (std::filesystem::exists(temp_home)) {
		std::filesystem::remove_all(temp_home);
	}
	std::filesystem::create_directories(temp_home);
	setenv("HOME", temp_home.c_str(), 1);

	// 2. Create a fake skill
	std::filesystem::path skill_dir = temp_home / ".copilot" / "skills" / "test_skill";
	write_file(skill_dir / "SKILL.md", "---\nname: test_skill\ndescription: Test skill description\n---\nTest Skill Content\n");

	// 3. Initialize skill_manager
	skill_manager::get_instance().initialize();

	// 4. Initialize tool registry and context
	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;

	// 5. Test basic valid tool execution
	std::string execute_result = registry.execute_tool("list_skills", "{}", ctx);
	std::cout << "Execution Result:\n" << execute_result << "\n";
	assert(execute_result.find("test_skill") != std::string::npos);
	assert(execute_result.find("Test skill description") != std::string::npos);

	// 6. Test unexpected parameter rejection (should fail validation)
	{
		auto prep = registry.prepare_tool("list_skills", "{\"unexpected_arg\": 123}", ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	// Clean up
	std::filesystem::remove_all(temp_home);
	std::cout << "list_skills tests passed successfully.\n";
	return 0;
}
