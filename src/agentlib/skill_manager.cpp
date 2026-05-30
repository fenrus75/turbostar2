#include "skill_manager.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <format>
#include "event_logger.h"
#include "markdown_utils.h"

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
	skills_.clear();
	vfs_ = std::make_unique<virtual_file_system>();

	const char *home_dir = std::getenv("HOME");
	if (!home_dir)
		return;

	std::filesystem::path skills_base = std::filesystem::path(home_dir) / ".copilot" / "skills";
	if (!std::filesystem::exists(skills_base) || !std::filesystem::is_directory(skills_base)) {
		return;
	}

	constexpr std::size_t kMaxDescriptionLength = 1024;

	try {
		for (const auto &entry : std::filesystem::recursive_directory_iterator(skills_base, std::filesystem::directory_options::skip_permission_denied)) {
			if (entry.is_regular_file() && entry.path().filename().string() == "SKILL.md") {
				std::ifstream file(entry.path());
				if (!file.is_open()) {
					event_logger::get_instance().log(std::format("skill_manager: Failed to open SKILL.md at {}", entry.path().string()));
					continue;
				}

				std::string line;
				if (std::getline(file, line) && markdown_utils::trim(line) == "---") {
					std::string name;
					std::string description;
					bool in_desc = false;
					while (std::getline(file, line)) {
						std::string trimmed = markdown_utils::trim(line);
						if (trimmed == "---") {
							break;
						}
						if (trimmed.starts_with("name:")) {
							name = markdown_utils::trim(trimmed.substr(5));
							in_desc = false;
						} else if (trimmed.starts_with("description:")) {
							description = markdown_utils::trim(trimmed.substr(12));
							in_desc = true;
						} else if (in_desc) {
							if (description.length() < kMaxDescriptionLength) {
								if (!description.empty())
									description += " ";
								description += trimmed;
							}
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
	} catch (const std::exception &e) {
		event_logger::get_instance().log(std::format("skill_manager: Error initializing skills: {}", e.what()));
	} catch (...) {
		event_logger::get_instance().log("skill_manager: Unknown error initializing skills");
	}
}

void skill_manager::scan_and_mount(const std::filesystem::path &base_dir, const std::string &skill_name)
{
	try {
		for (const auto &entry : std::filesystem::recursive_directory_iterator(base_dir, std::filesystem::directory_options::skip_permission_denied)) {
			if (entry.is_regular_file()) {
				// Compute relative path
				std::string rel_path = std::filesystem::relative(entry.path(), base_dir).string();

				// Construct URI: skills://skill_name/rel_path
				std::string uri = "skills://" + skill_name + "/" + rel_path;

				vfs_->mount_file(uri, entry.path().string());
			}
		}
	} catch (const std::exception &e) {
		event_logger::get_instance().log(std::format("skill_manager: Error scanning and mounting skills: {}", e.what()));
	} catch (...) {
		event_logger::get_instance().log("skill_manager: Unknown error scanning and mounting skills");
	}
}

} // namespace agentlib
