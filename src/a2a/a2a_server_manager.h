#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>

namespace a2a
{

enum class a2a_server_tier {
	global_system,
	project_local,
	ephemeral_runtime
};

struct a2a_server_config {
	std::string name;
	std::string url;
	std::string auth_token;
	a2a_server_tier tier = a2a_server_tier::ephemeral_runtime;
};

class a2a_server_manager
{
      public:
	static a2a_server_manager &get_instance();

	void initialize();
	void load_global_servers();
	void load_project_servers();
	void save_global_servers();
	void save_project_servers();

	void add_server(const a2a_server_config &config);
	bool remove_server(std::string_view name, std::optional<a2a_server_tier> tier = std::nullopt);
	std::optional<a2a_server_config> find_server(std::string_view name) const;
	std::vector<a2a_server_config> get_all_servers() const;
	void clear_ephemeral_servers();

      private:
	a2a_server_manager() = default;
	~a2a_server_manager() = default;

	/*
	 * servers_mutex_ protects access to global_servers_, project_servers_, and ephemeral_servers_
	 * when loading, saving, modifying, or querying server configurations across worker threads.
	 * Callers must hold servers_mutex_ in shared or exclusive mode before accessing server vectors.
	 */
	mutable std::shared_mutex servers_mutex_;
	std::vector<a2a_server_config> global_servers_;
	std::vector<a2a_server_config> project_servers_;
	std::vector<a2a_server_config> ephemeral_servers_;
};

} // namespace a2a
