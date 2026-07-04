#include "skill_manager.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <format>
#include <sstream>
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

void skill_manager::register_skill(const std::string &skill_content, bool visible)
{
	std::map<std::string, std::string> files;
	files["SKILL.md"] = skill_content;
	register_skill(files, visible);
}

void skill_manager::register_skill(const std::map<std::string, std::string> &files, bool visible)
{
	auto it = files.find("SKILL.md");
	if (it == files.end()) {
		event_logger::get_instance().log("skill_manager: Failed to register skill: SKILL.md not found in files map.");
		return;
	}

	const std::string &skill_content = it->second;

	// Parse the frontmatter from SKILL.md content
	std::stringstream ss(skill_content);
	std::string line;
	std::string name;
	std::string description;

	if (std::getline(ss, line) && utf8::trim(line) == "---") {
		std::string frontmatter;
		bool in_frontmatter = true;
		while (std::getline(ss, line)) {
			std::string trimmed = utf8::trim(line);
			if (trimmed == "---") {
				in_frontmatter = false;
				break;
			}
			frontmatter += line + "\n";
		}

		if (!in_frontmatter) {
			try {
				YAML::Node config = YAML::Load(frontmatter);
				if (config["name"]) {
					name = config["name"].as<std::string>();
				}
				if (config["description"]) {
					description = config["description"].as<std::string>();
				}
			} catch (const std::exception &e) {
				event_logger::get_instance().log("skill_manager: YAML parse error during dynamic registration: {}", e.what());
				return;
			}
		}
	}

	if (name.empty()) {
		event_logger::get_instance().log("skill_manager: Failed to register skill: Could not parse name from SKILL.md frontmatter.");
		return;
	}

	// 1. Register the skill metadata
	register_skill(name, description, "skills://" + name + "/", visible);

	// 2. Mount all files in the map to VFS under skills://<name>/
	for (const auto &pair : files) {
		std::string uri = "skills://" + name + "/" + pair.first;
		vfs_->mount_buffer(uri, pair.second);
	}
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

std::string skill_manager::format_skill_content(const std::string &name) const
{
	std::lock_guard<std::mutex> lock(mutex_);
	const skill *target = nullptr;
	for (const auto &s : skills_) {
		if (s.name == name) {
			target = &s;
			break;
		}
	}
	if (!target) {
		return "Error: Skill '" + name + "' not found.";
	}

	std::string base_uri = target->uri;
	if (!base_uri.empty() && base_uri.back() != '/') {
		base_uri += '/';
	}

	std::string skill_md_uri = base_uri + "SKILL.md";
	auto view_opt = vfs_->read_file(skill_md_uri);

	std::string instructions;
	if (view_opt) {
		instructions = std::string(view_opt.value()->view());
	} else {
		instructions = "Error: SKILL.md not found in skill root.";
	}

	std::stringstream ss;
	ss << "<skill_content name=\"" << name << "\">\n";
	ss << instructions << "\n\n";
	ss << "Skill directory: `" << base_uri << "`\n";
	ss << "Relative paths in this skill are relative to the skill directory.\n\n";
	ss << "<skill_resources>\n";

	auto entries = vfs_->list_directory(base_uri);
	for (const auto &entry : entries) {
		if (entry.uri.rfind(base_uri, 0) != 0)
			continue;

		std::string filename = entry.uri.substr(base_uri.length());
		if (filename.empty())
			continue;

		if (entry.type == 'D')
			continue;

		ss << "  <file>" << filename << "</file>\n";
	}

	ss << "</skill_resources>\n";
	ss << "</skill_content>";

	return ss.str();
}

void skill_manager::unregister_skill(const std::string &name)
{
	std::lock_guard<std::mutex> lock(mutex_);
	for (auto it = skills_.begin(); it != skills_.end(); ++it) {
		if (it->name == name) {
			skills_.erase(it);
			break;
		}
	}
	vfs_->unmount_prefix("skills://" + name + "/");
}

} // namespace agentlib
