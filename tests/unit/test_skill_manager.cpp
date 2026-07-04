#include "test_watchdog.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include "agentlib/skill_manager.h"
#include "event_logger.h"

using namespace agentlib;

// Helper to write file contents
void write_file(const std::filesystem::path& path, const std::string& content)
{
	std::filesystem::create_directories(path.parent_path());
	std::ofstream out(path);
	out << content;
}

void test_robust_skill_parsing()
{
	// 1. Create a temporary home directory
	std::filesystem::path temp_home = std::filesystem::absolute("./test_skills_home");
	if (std::filesystem::exists(temp_home)) {
		std::filesystem::remove_all(temp_home);
	}
	std::filesystem::create_directories(temp_home);

	// Set HOME environment variable
	setenv("HOME", temp_home.c_str(), 1);

	// 2. Create a skill with trailing whitespaces in delimiters and keys
	std::filesystem::path skill_dir = temp_home / ".copilot" / "skills" / "test_skill";
	std::filesystem::create_directories(skill_dir);

	std::string skill_content = 
		"---  \n"  // Trailing space in delimiter
		"  name: test_name  \n"  // Leading and trailing spaces
		"  description: a description with spaces   \n"  // Leading/trailing spaces
		"--- \n"  // Trailing space in end delimiter
		"Some extra content here.\n";

	write_file(skill_dir / "SKILL.md", skill_content);
	write_file(skill_dir / "some_file.txt", "File contents");

	// 3. Initialize skill manager
	auto& manager = skill_manager::get_instance();
	manager.initialize();

	// 4. Verify skill is parsed and trailing/leading spaces are trimmed
	const auto& skills = manager.get_skills();
	
	// We check if the skill was successfully parsed
	assert(!skills.empty());
	
	bool found = false;
	for (const auto& s : skills) {
		if (s.name == "test_name") {
			found = true;
			assert(s.description == "a description with spaces");
			assert(s.uri == "skills://test_name/");
		}
	}
	assert(found);

	// Clean up
	std::filesystem::remove_all(temp_home);
}

void test_hidden_skills()
{
	auto& manager = skill_manager::get_instance();
	manager.initialize();

	// Register a dynamic visible skill
	manager.register_skill("visible_test", "Visible skill desc", "skills://visible_test/", true);
	// Register a dynamic hidden skill
	manager.register_skill("hidden_test", "Hidden skill desc", "skills://hidden_test/", false);

	const auto& skills = manager.get_skills();
	bool found_visible = false;
	bool found_hidden = false;

	for (const auto& s : skills) {
		if (s.name == "visible_test") {
			found_visible = true;
			assert(s.visible == true);
		}
		if (s.name == "hidden_test") {
			found_hidden = true;
			assert(s.visible == false);
		}
	}
	assert(found_visible);
	assert(found_hidden);

	// Verify we can change visibility
	manager.set_visibility("hidden_test", true);
	const auto& skills_updated = manager.get_skills();
	bool found_updated = false;
	for (const auto& s : skills_updated) {
		if (s.name == "hidden_test") {
			assert(s.visible == true);
			found_updated = true;
		}
	}
	assert(found_updated);
}

void test_dynamic_registration()
{
	auto& manager = skill_manager::get_instance();
	manager.initialize();

	// 1. Register via string content
	std::string string_skill = 
		"---\n"
		"name: string_skill_test\n"
		"description: String skill description\n"
		"---\n"
		"String instructions go here.\n";

	manager.register_skill(string_skill, false);

	const auto& skills = manager.get_skills();
	bool found_string_skill = false;
	for (const auto& s : skills) {
		if (s.name == "string_skill_test") {
			found_string_skill = true;
			assert(s.description == "String skill description");
			assert(s.visible == false);
		}
	}
	assert(found_string_skill);

	// Verify it was mounted in VFS
	auto view_opt = manager.get_vfs()->read_file("skills://string_skill_test/SKILL.md");
	assert(view_opt);
	assert(std::string(view_opt.value()->view()) == string_skill);

	// 2. Register via map of files
	std::map<std::string, std::string> files;
	std::string map_skill = 
		"---\n"
		"name: map_skill_test\n"
		"description: Map skill description\n"
		"---\n"
		"Map instructions go here.\n";
	files["SKILL.md"] = map_skill;
	files["helper.txt"] = "helper content";

	manager.register_skill(files, true);

	const auto& skills2 = manager.get_skills();
	bool found_map_skill = false;
	for (const auto& s : skills2) {
		if (s.name == "map_skill_test") {
			found_map_skill = true;
			assert(s.description == "Map skill description");
			assert(s.visible == true);
		}
	}
	assert(found_map_skill);

	// Verify both files were mounted
	auto skill_md = manager.get_vfs()->read_file("skills://map_skill_test/SKILL.md");
	assert(skill_md);
	assert(std::string(skill_md.value()->view()) == map_skill);

	auto helper = manager.get_vfs()->read_file("skills://map_skill_test/helper.txt");
	assert(helper);
	assert(std::string(helper.value()->view()) == "helper content");

	// 3. Test unregistering
	manager.unregister_skill("map_skill_test");
	const auto& skills3 = manager.get_skills();
	bool found_after_unreg = false;
	for (const auto& s : skills3) {
		if (s.name == "map_skill_test") {
			found_after_unreg = true;
		}
	}
	assert(!found_after_unreg);

	// VFS prefix should be unmounted
	auto unreg_skill_md = manager.get_vfs()->read_file("skills://map_skill_test/SKILL.md");
	assert(!unreg_skill_md);
}

int main()
{
	test_watchdog::setup_watchdog(30);
	test_robust_skill_parsing();
	test_hidden_skills();
	test_dynamic_registration();
	std::cout << "skill_manager tests passed.\n";
	return 0;
}
