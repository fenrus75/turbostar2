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

int main()
{
	test_watchdog::setup_watchdog(30);
	test_robust_skill_parsing();
	test_hidden_skills();
	std::cout << "skill_manager tests passed.\n";
	return 0;
}
