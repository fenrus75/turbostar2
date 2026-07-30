#include "a2a/a2a_server_manager.h"
#include <fstream>
#include "event_logger.h"
#include "fs_utils.h"

namespace fs = std::filesystem;

namespace a2a
{

a2a_server_manager &a2a_server_manager::get_instance()
{
	static a2a_server_manager instance;
	return instance;
}

void a2a_server_manager::initialize()
{
	load_global_servers();
	load_project_servers();
}

static std::vector<a2a_server_config> load_servers_from_file(const fs::path &path, a2a_server_tier tier)
{
	std::vector<a2a_server_config> result;
	if (!fs::exists(path)) {
		return result;
	}

	try {
		std::ifstream file(path);
		if (!file.is_open()) {
			return result;
		}

		nlohmann::json j;
		file >> j;

		if (j.is_array()) {
			for (const auto &item : j) {
				if (item.contains("name") && item.contains("url")) {
					a2a_server_config cfg;
					cfg.name = item.value("name", "");
					cfg.url = item.value("url", "");
					cfg.auth_token = item.value("auth_token", "");
					cfg.tier = tier;
					if (!cfg.name.empty() && !cfg.url.empty()) {
						result.push_back(cfg);
					}
				}
			}
		}
	} catch (const std::exception &e) {
		event_logger::get_instance().log("Failed to parse A2A servers file '{}': {}", path.string(), e.what());
	}

	return result;
}

static void save_servers_to_file(const fs::path &path, const std::vector<a2a_server_config> &servers)
{
	try {
		if (path.has_parent_path() && !fs::exists(path.parent_path())) {
			fs::create_directories(path.parent_path());
		}

		nlohmann::json j = nlohmann::json::array();
		for (const auto &cfg : servers) {
			nlohmann::json item;
			item["name"] = cfg.name;
			item["url"] = cfg.url;
			if (!cfg.auth_token.empty()) {
				item["auth_token"] = cfg.auth_token;
			}
			j.push_back(item);
		}

		std::ofstream file(path);
		if (file.is_open()) {
			file << j.dump(4) << std::endl;
		}
	} catch (const std::exception &e) {
		event_logger::get_instance().log("Failed to save A2A servers to file '{}': {}", path.string(), e.what());
	}
}

void a2a_server_manager::load_global_servers()
{
	fs::path global_path = fs::path(fs_utils::get_global_cache_dir()) / "a2a_servers.json";
	auto loaded = load_servers_from_file(global_path, a2a_server_tier::global_system);

	std::unique_lock lock(servers_mutex_);
	global_servers_ = std::move(loaded);
}

void a2a_server_manager::load_project_servers()
{
	fs::path proj_path = fs::path(fs_utils::get_project_cache_root()) / "a2a_servers.json";
	auto loaded = load_servers_from_file(proj_path, a2a_server_tier::project_local);

	std::unique_lock lock(servers_mutex_);
	project_servers_ = std::move(loaded);
}

void a2a_server_manager::save_global_servers()
{
	std::shared_lock lock(servers_mutex_);
	fs::path global_path = fs::path(fs_utils::get_global_cache_dir()) / "a2a_servers.json";
	save_servers_to_file(global_path, global_servers_);
}

void a2a_server_manager::save_project_servers()
{
	std::shared_lock lock(servers_mutex_);
	fs::path proj_path = fs::path(fs_utils::get_project_cache_root()) / "a2a_servers.json";
	save_servers_to_file(proj_path, project_servers_);
}

void a2a_server_manager::add_server(const a2a_server_config &config)
{
	std::unique_lock lock(servers_mutex_);
	auto remove_from_vec = [&](std::vector<a2a_server_config> &vec) {
		std::erase_if(vec, [&](const a2a_server_config &s) { return s.name == config.name; });
	};

	switch (config.tier) {
	case a2a_server_tier::ephemeral_runtime:
		remove_from_vec(ephemeral_servers_);
		ephemeral_servers_.push_back(config);
		break;
	case a2a_server_tier::project_local:
		remove_from_vec(project_servers_);
		project_servers_.push_back(config);
		break;
	case a2a_server_tier::global_system:
		remove_from_vec(global_servers_);
		global_servers_.push_back(config);
		break;
	}
}

bool a2a_server_manager::remove_server(std::string_view name, std::optional<a2a_server_tier> tier)
{
	std::unique_lock lock(servers_mutex_);
	bool removed = false;

	auto try_remove = [&](std::vector<a2a_server_config> &vec) {
		auto old_sz = vec.size();
		std::erase_if(vec, [&](const a2a_server_config &s) { return s.name == name; });
		if (vec.size() < old_sz) {
			removed = true;
		}
	};

	if (!tier.has_value() || tier.value() == a2a_server_tier::ephemeral_runtime) {
		try_remove(ephemeral_servers_);
	}
	if (!tier.has_value() || tier.value() == a2a_server_tier::project_local) {
		try_remove(project_servers_);
	}
	if (!tier.has_value() || tier.value() == a2a_server_tier::global_system) {
		try_remove(global_servers_);
	}

	return removed;
}

std::optional<a2a_server_config> a2a_server_manager::find_server(std::string_view name) const
{
	std::shared_lock lock(servers_mutex_);

	// Priority: Ephemeral > Project > Global
	for (const auto &s : ephemeral_servers_) {
		if (s.name == name) return s;
	}
	for (const auto &s : project_servers_) {
		if (s.name == name) return s;
	}
	for (const auto &s : global_servers_) {
		if (s.name == name) return s;
	}

	return std::nullopt;
}

std::vector<a2a_server_config> a2a_server_manager::get_all_servers() const
{
	std::shared_lock lock(servers_mutex_);
	std::vector<a2a_server_config> all;
	std::unordered_map<std::string, bool> seen;

	// Priority: Ephemeral > Project > Global
	for (const auto &s : ephemeral_servers_) {
		if (!seen[s.name]) {
			all.push_back(s);
			seen[s.name] = true;
		}
	}
	for (const auto &s : project_servers_) {
		if (!seen[s.name]) {
			all.push_back(s);
			seen[s.name] = true;
		}
	}
	for (const auto &s : global_servers_) {
		if (!seen[s.name]) {
			all.push_back(s);
			seen[s.name] = true;
		}
	}

	return all;
}

void a2a_server_manager::clear_ephemeral_servers()
{
	std::unique_lock lock(servers_mutex_);
	ephemeral_servers_.clear();
}

} // namespace a2a
