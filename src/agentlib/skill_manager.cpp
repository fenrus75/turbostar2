#include "skill_manager.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <format>
#include <yaml-cpp/yaml.h>
#include "event_logger.h"
#include "utf8.h"

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
	std::lock_guard<std::mutex> lock(mutex_);
	return skills_;
}

void skill_manager::register_skill(const std::string &name, const std::string &description, const std::string &uri, bool visible)
{
	std::lock_guard<std::mutex> lock(mutex_);
	for (auto &s : skills_) {
		if (s.name == name) {
			s.description = description;
			s.uri = uri;
			s.visible = visible;
			return;
		}
	}
	skill new_skill;
	new_skill.name = name;
	new_skill.description = description;
	new_skill.uri = uri;
	new_skill.visible = visible;
	skills_.push_back(new_skill);
}

void skill_manager::set_visibility(const std::string &name, bool visible)
{
	std::lock_guard<std::mutex> lock(mutex_);
	for (auto &s : skills_) {
		if (s.name == name) {
			s.visible = visible;
			return;
		}
	}
}

void skill_manager::initialize()
{
	std::lock_guard<std::mutex> lock(mutex_);
	skills_.clear();
	vfs_ = std::make_unique<virtual_file_system>();

	const char *home_dir = std::getenv("HOME");
	if (!home_dir)
		return;

	std::filesystem::path skills_base = std::filesystem::path(home_dir) / ".copilot" / "skills";
	if (!std::filesystem::exists(skills_base) || !std::filesystem::is_directory(skills_base)) {
		return;
	}
	try {
		for (const auto &entry : std::filesystem::recursive_directory_iterator(skills_base, std::filesystem::directory_options::skip_permission_denied)) {
			if (entry.is_regular_file() && entry.path().filename().string() == "SKILL.md") {
				std::ifstream file(entry.path());
				if (!file.is_open()) {
					event_logger::get_instance().log("skill_manager: Failed to open SKILL.md at {}", entry.path().string());
					continue;
				}

				std::string line;
				if (std::getline(file, line) && utf8::trim(line) == "---") {
					std::string frontmatter;
					while (std::getline(file, line)) {
						std::string trimmed = utf8::trim(line);
						if (trimmed == "---") {
							break;
						}
						frontmatter += line + "\n";
					}

					try {
						YAML::Node config = YAML::Load(frontmatter);
						std::string name;
						if (config["name"]) {
							name = config["name"].as<std::string>();
						}
						std::string description;
						if (config["description"]) {
							description = config["description"].as<std::string>();
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
					} catch (const std::exception &e) {
						event_logger::get_instance().log("skill_manager: YAML parse error at {}: {}", entry.path().string(), e.what());
					}
				}
			}
		}
	} catch (const std::exception &e) {
		event_logger::get_instance().log("skill_manager: Error initializing skills: {}", e.what());
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
		event_logger::get_instance().log("skill_manager: Error scanning and mounting skills: {}", e.what());
	} catch (...) {
		event_logger::get_instance().log("skill_manager: Unknown error scanning and mounting skills");
	}
}

} // namespace agentlib
