#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/skill_manager.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/agentlib/virtual_file_system.h"

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

	// Clean up
	std::filesystem::remove_all(temp_home);
	std::cout << "activate_skill tests passed successfully.\n";
	return 0;
}
