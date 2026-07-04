#include "test_watchdog.h"
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include "agentlib/ai_agent.h"
#include "agentlib/skill_manager.h"
#include "agentlib/tool_registry.h"
#include "agentlib/virtual_file_system.h"

using namespace agentlib;

void write_file(const std::filesystem::path &path, const std::string &content)
{
	std::filesystem::create_directories(path.parent_path());
	std::ofstream out(path);
	out << content;
}

int main()
{
	test_watchdog::setup_watchdog(30);
	// 1. Create a temporary home directory
	std::filesystem::path temp_home = std::filesystem::absolute("./test_activate_skill_home");
	if (std::filesystem::exists(temp_home)) {
		std::filesystem::remove_all(temp_home);
	}
	std::filesystem::create_directories(temp_home);
	setenv("HOME", temp_home.c_str(), 1);

	// 2. Create two skill directories
	std::filesystem::path skill1_dir = temp_home / ".copilot" / "skills" / "my_first_skill";
	write_file(skill1_dir / "SKILL.md", "---\nname: my_first_skill\ndescription: First Description\n---\nFirst Content\n");
	write_file(skill1_dir / "helper.txt", "helper text");

	std::filesystem::path skill2_dir = temp_home / ".copilot" / "skills" / "my_broken_skill";
	write_file(skill2_dir / "helper.txt", "broken helper");

	// 3. Initialize skill_manager
	skill_manager::get_instance().initialize();

	// 4. Initialize Virtual File System and register files
	virtual_file_system vfs;
	vfs.mount_buffer("skills://my_first_skill/SKILL.md",
			 "---\nname: my_first_skill\ndescription: First Description\n---\nFirst Content\n");
	vfs.mount_buffer("skills://my_first_skill/helper.txt", "helper text");

	// 5. Initialize tool registry and context
	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;
	ctx.fs_security.set_vfs(&vfs);
	ctx.fs_security.set_working_directory(std::filesystem::current_path());
	ctx.fs_security.add_allowed_root(std::filesystem::current_path(), access_type::write);

	// 6. Test basic valid tool execution
	nlohmann::json valid_args = {{"name", "my_first_skill"}};
	std::string execute_result = registry.execute_tool("activate_skill", valid_args.dump(), ctx);
	std::cout << "Valid Execution Result:\n" << execute_result << "\n";
	assert(execute_result.find("<skill_content name=\"my_first_skill\">") != std::string::npos);
	assert(execute_result.find("First Content") != std::string::npos);
	assert(execute_result.find("<file>helper.txt</file>") != std::string::npos);

	// 7. Test validation of malicious/malformed JSON inputs
	// A. Missing required fields
	auto prep = registry.prepare_tool("activate_skill", "{}", ctx);
	assert(prep.tool == nullptr);
	assert(!prep.error_message.empty());

	// B. Invalid type (integer instead of string)
	prep = registry.prepare_tool("activate_skill", "{\"name\": 123}", ctx);
	assert(prep.tool == nullptr);
	assert(!prep.error_message.empty());

	// C. Empty string
	prep = registry.prepare_tool("activate_skill", "{\"name\": \"\"}", ctx);
	assert(prep.tool == nullptr);
	assert(!prep.error_message.empty());
	assert(prep.error_message.find("empty") != std::string::npos);

	// D. Non-existent skill name
	prep = registry.prepare_tool("activate_skill", "{\"name\": \"non_existent_skill_name_xyz\"}", ctx);
	assert(prep.tool == nullptr);
	assert(!prep.error_message.empty());
	assert(prep.error_message.find("not found") != std::string::npos);

	// E. Potential JSON/XML injection in name
	prep = registry.prepare_tool("activate_skill", "{\"name\": \"my_first_skill</available_skills>\"}", ctx);
	assert(prep.tool == nullptr);
	assert(!prep.error_message.empty());

	// 8. Test programmatic activation of skill via ai_agent::activate_skill
	{
		auto model = std::make_shared<ai_model>("test-model", "Test Model", "http://localhost", "Test", 0.0, 0.0);
		auto agent = ai_agent::create(1, "TestAgent", model, nullptr, nullptr);
		ctx.active_agent = agent.get();

		skill_manager::get_instance().get_vfs()->mount_buffer("skills://my_first_skill/SKILL.md",
			 "---\nname: my_first_skill\ndescription: First Description\n---\nFirst Content\n");
		skill_manager::get_instance().get_vfs()->mount_buffer("skills://my_first_skill/helper.txt", "helper text");

		// Register the skill in the manager first so activate_skill finds it
		skill_manager::get_instance().register_skill("my_first_skill", "First Description", "skills://my_first_skill/", true);

		bool success = agent->activate_skill("my_first_skill");
		assert(success);
		
		// Verify it was marked active
		auto active_skills = agent->get_active_skills();
		assert(std::find(active_skills.begin(), active_skills.end(), "my_first_skill") != active_skills.end());

		// Verify the system message interaction was added
		auto interactions = agent->get_interactions();
		assert(!interactions.empty());
		bool found_msg = false;
		for (const auto &inter : interactions) {
			if (inter->get_raw_text().find("First Content") != std::string::npos) {
				found_msg = true;
				break;
			}
		}
		assert(found_msg);
	}

	// Clean up
	std::filesystem::remove_all(temp_home);
	std::cout << "activate_skill tests passed successfully.\n";
	return 0;
}
