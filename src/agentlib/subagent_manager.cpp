#include "subagent_manager.h"
#include "project_manager.h"
#include "event_logger.h"
#include "utf8.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <yaml-cpp/yaml.h>

// Generated headers for embedded agents
#include "research_agent.h"
#include "self_agent.h"
#include "agent_animation.h"

namespace agentlib
{

static std::filesystem::path expand_path(const std::string &path_str)
{
	if (path_str.empty()) return "";
	if (path_str[0] == '~') {
		const char *home = std::getenv("HOME");
		if (!home) return "";
		if (path_str.length() == 1) return home;
		// Handle ~/ or ~/...
		if (path_str[1] == '/') {
			return std::filesystem::path(home) / path_str.substr(2);
		}
		return std::filesystem::path(home) / path_str.substr(1);
	}
	return path_str;
}

static std::optional<subagent> parse_subagent_content(const std::string &content, const std::string &origin)
{
	size_t first_delim = content.find("---");
	if (first_delim == std::string::npos) {
		return std::nullopt;
	}
	size_t frontmatter_start = first_delim + 3;
	size_t second_delim = content.find("---", frontmatter_start);
	if (second_delim == std::string::npos) {
		return std::nullopt;
	}

	std::string frontmatter = content.substr(frontmatter_start, second_delim - frontmatter_start);
	std::string system_prompt = utf8::trim(content.substr(second_delim + 3));

	try {
		YAML::Node config = YAML::Load(frontmatter);
		std::string name;
		if (config["name"]) {
			name = utf8::trim(config["name"].as<std::string>());
		}
		if (name.empty()) {
			return std::nullopt;
		}

		subagent sa;
		sa.name = name;
		sa.system_prompt = system_prompt;
		sa.file_path = origin;

		if (config["description"]) {
			sa.description = utf8::trim(config["description"].as<std::string>());
		}
		if (config["model"]) {
			sa.model = utf8::trim(config["model"].as<std::string>());
		}

		// tools
		if (config["tools"]) {
			if (config["tools"].IsSequence()) {
				for (const auto &t : config["tools"]) {
					sa.tools.push_back(utf8::trim(t.as<std::string>()));
				}
			}
		}

		// tool_families / toolFamilies
		auto load_families = [&](const std::string &key) {
			if (config[key]) {
				if (config[key].IsSequence()) {
					for (const auto &f : config[key]) {
						sa.tool_families.push_back(utf8::trim(f.as<std::string>()));
					}
				}
			}
		};
		load_families("tool_families");
		load_families("toolFamilies");

		// read_only / readOnly
		if (config["read_only"]) {
			sa.read_only = config["read_only"].as<bool>();
		} else if (config["readOnly"]) {
			sa.read_only = config["readOnly"].as<bool>();
		}

		// permission_mode / permissionMode
		if (config["permission_mode"]) {
			sa.permission_mode = utf8::trim(config["permission_mode"].as<std::string>());
		} else if (config["permissionMode"]) {
			sa.permission_mode = utf8::trim(config["permissionMode"].as<std::string>());
		}

		// effort
		if (config["effort"]) {
			sa.effort = utf8::trim(config["effort"].as<std::string>());
		}

		// max_turns / maxTurns
		if (config["max_turns"]) {
			sa.max_turns = config["max_turns"].as<int>();
		} else if (config["maxTurns"]) {
			sa.max_turns = config["maxTurns"].as<int>();
		}

		// animation
		if (config["animation"]) {
			sa.animation_path = utf8::trim(config["animation"].as<std::string>());
			if (!sa.animation_path.empty()) {
				if (sa.animation_path.ends_with(".json")) {
					std::filesystem::path anim_file(sa.animation_path);
					if (anim_file.is_relative() && !origin.empty() && !origin.starts_with("builtin://") && !origin.starts_with("plugin://")) {
						anim_file = std::filesystem::path(origin).parent_path() / anim_file;
					}
					if (std::filesystem::exists(anim_file)) {
						std::ifstream f(anim_file);
						if (f.is_open()) {
							std::stringstream anim_ss;
							anim_ss << f.rdbuf();
							std::string anim_json = anim_ss.str();
							if (agent_animation_registry::get_instance().register_animation_json(sa.name, anim_json)) {
								sa.animation_name = sa.name;
							}
						}
					}
				} else {
					sa.animation_name = sa.animation_path;
				}
			}
		}

		return sa;
	}
	catch (const std::exception &e) {
		event_logger::get_instance().log("subagent_manager: Error parsing yaml metadata for origin {}: {}", origin, e.what());
		return std::nullopt;
	}
}

subagent_manager &subagent_manager::get_instance()
{
	static subagent_manager instance;
	return instance;
}

void subagent_manager::initialize()
{
	subagents_.clear();
	load_builtins();

	// Scan global paths
	scan_directory(expand_path("~/.cache/turbostar/agents"));
	scan_directory(expand_path("~/.gemini/config/agents"));
	scan_directory(expand_path("~/gemini/config/agents"));
	scan_directory(expand_path("~/.agents"));

	// Scan workspace root (highest priority)
	std::string root = project_manager::get_instance().get_project_root();
	if (!root.empty()) {
		scan_directory(std::filesystem::path(root) / ".agents");
	}
}

const std::vector<subagent> &subagent_manager::get_subagents() const
{
	return subagents_;
}

std::optional<subagent> subagent_manager::find_subagent_by_name(const std::string &name) const
{
	for (const auto &sa : subagents_) {
		if (sa.name == name) {
			return sa;
		}
	}
	return std::nullopt;
}

void subagent_manager::load_builtins()
{
	load_from_string(std::string(research_agent_md), "builtin://research");
	load_from_string(std::string(self_agent_md), "builtin://self");
}

void subagent_manager::load_from_string(const std::string &content, const std::string &origin)
{
	auto sa = parse_subagent_content(content, origin);
	if (sa) {
		// Overwrite if exists
		auto it = std::remove_if(subagents_.begin(), subagents_.end(),
			[&](const subagent &s) { return s.name == sa->name; });
		if (it != subagents_.end()) {
			subagents_.erase(it, subagents_.end());
		}
		subagents_.push_back(*sa);
	}
}

void subagent_manager::scan_directory(const std::filesystem::path &dir)
{
	if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
		return;
	}

	try {
		for (const auto &entry : std::filesystem::recursive_directory_iterator(dir, std::filesystem::directory_options::skip_permission_denied)) {
			if (entry.is_regular_file() && entry.path().extension() == ".md") {
				auto sa = parse_subagent_file(entry.path());
				if (sa) {
					// Overwrite if exists (higher priority overrides lower/earlier loads)
					auto it = std::remove_if(subagents_.begin(), subagents_.end(),
						[&](const subagent &s) { return s.name == sa->name; });
					if (it != subagents_.end()) {
						subagents_.erase(it, subagents_.end());
					}
					subagents_.push_back(*sa);
				}
			}
		}
	}
	catch (const std::exception &e) {
		event_logger::get_instance().log("subagent_manager: Error scanning directory {}: {}", dir.string(), e.what());
	}
}

std::optional<subagent> subagent_manager::parse_subagent_file(const std::filesystem::path &path)
{
	std::ifstream file(path);
	if (!file.is_open()) {
		return std::nullopt;
	}

	std::stringstream ss;
	ss << file.rdbuf();
	return parse_subagent_content(ss.str(), path.string());
}

void subagent_manager::register_subagent(const std::string &name, const std::string &text, const std::string &animation_json)
{
	auto sa = parse_subagent_content(text, "plugin://" + name);
	if (sa) {
		sa->name = name; // Force the name requested by registration

		if (!animation_json.empty()) {
			if (agent_animation_registry::get_instance().register_animation_json(name, animation_json)) {
				sa->animation_name = name;
			}
		}

		// Overwrite if exists
		auto it = std::remove_if(subagents_.begin(), subagents_.end(),
			[&](const subagent &s) { return s.name == name; });
		if (it != subagents_.end()) {
			subagents_.erase(it, subagents_.end());
		}
		subagents_.push_back(*sa);
	}
}

void subagent_manager::unregister_subagent(const std::string &name)
{
	// Clean up animation registry entry if registered under subagent's name
	agent_animation_registry::get_instance().unregister_animation(name);

	auto it = std::remove_if(subagents_.begin(), subagents_.end(),
		[&](const subagent &s) { return s.name == name; });
	if (it != subagents_.end()) {
		subagents_.erase(it, subagents_.end());
	}
}


} // namespace agentlib
