#include "skill_manager.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include "tool_validator.h"

namespace agentlib
{

skill_manager &skill_manager::get_instance()
{
	static skill_manager instance;
	return instance;
}

virtual_file_system *skill_manager::get_vfs()
{
	return vfs_.get();
}

const std::vector<skill> &skill_manager::get_skills() const
{
	return skills_;
}

void skill_manager::initialize()
{
	const char *home_dir = std::getenv("HOME");
	if (!home_dir)
		return;

	std::filesystem::path skills_base = std::filesystem::path(home_dir) / ".copilot" / "skills";
	if (!std::filesystem::exists(skills_base) || !std::filesystem::is_directory(skills_base)) {
		return;
	}

	try {
		for (const auto &entry : std::filesystem::recursive_directory_iterator(skills_base)) {
			if (entry.is_regular_file() && entry.path().filename() == "SKILL.md") {
				std::ifstream file(entry.path());
				std::string line;
				if (std::getline(file, line) && line == "---") {
					std::string name;
					std::string description;
					bool in_desc = false;
					while (std::getline(file, line) && line != "---") {
						if (line.starts_with("name:")) {
							name = line.substr(5);
							name.erase(0, name.find_first_not_of(" \t"));
							in_desc = false;
						} else if (line.starts_with("description:")) {
							description = line.substr(12);
							description.erase(0, description.find_first_not_of(" \t"));
							in_desc = true;
						} else if (in_desc) {
							if (description.length() < 1024) {
								if (!description.empty())
									description += " ";
								description += line;
							}
						} else {
							in_desc = false;
						}
					}

					if (!name.empty()) {
						std::filesystem::path physical_root = entry.path().parent_path();

						skill new_skill;
						new_skill.name = name;
						new_skill.description = description;
						new_skill.uri = "skills://" + name + "/";
						skills_.push_back(new_skill);

						scan_and_mount(physical_root, name);
					}
				}
			}
		}
	} catch (...) {
		// Silently ignore errors as this is an optional feature
	}
}

void skill_manager::scan_and_mount(const std::filesystem::path &base_dir, const std::string &skill_name)
{
	try {
		for (const auto &entry : std::filesystem::recursive_directory_iterator(base_dir)) {
			if (entry.is_regular_file()) {
				// Compute relative path
				std::string rel_path = std::filesystem::relative(entry.path(), base_dir).string();

				// Construct URI: skills://skill_name/rel_path
				std::string uri = "skills://" + skill_name + "/" + rel_path;

				vfs_->mount_file(uri, entry.path().string());
			}
		}
	} catch (...) {
		// Silently ignore access errors
	}
}

} // namespace agentlib
