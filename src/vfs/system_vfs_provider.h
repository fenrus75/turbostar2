#pragma once

#include "agentlib/virtual_file_system.h"
#include <functional>
#include <map>
#include <mutex>
#include <string>

namespace turbostar {

using vfs_generator_fn = std::function<std::string(const std::string &query)>;

/**
 * @brief VFS provider for the system:// scheme.
 * Serves embedded static markdown documents (e.g. system://languages/cpp23.md)
 * and dynamic runtime generators (e.g. system://agents.md, system://tools.md, system://tools_detailed.md, system://mcp.md).
 */
class system_vfs_provider : public agentlib::vfs_provider
{
      public:
	system_vfs_provider();
	~system_vfs_provider() override = default;

	bool exists(const std::string &uri) const override;
	std::optional<agentlib::vfs_file_handle> read_file(const std::string &uri) override;
	std::optional<agentlib::vfs_file_info> get_file_info(const std::string &uri) const override;
	std::vector<agentlib::vfs_file_info> list_directory(const std::string &prefix) const override;

	/**
	 * @brief Registers a dynamic file generator for a system URI.
	 */
	void register_generator(const std::string &path, vfs_generator_fn generator);

      private:
	std::string resolve_path(const std::string &uri, std::string *out_query = nullptr) const;

	/*
	 * generators_mutex_ protects the generators_ map from concurrent registration or invocation.
	 * Locking Rules:
	 * - Read or write lock acquired when accessing or modifying registered generator lambdas.
	 */
	mutable std::mutex generators_mutex_;
	std::map<std::string, vfs_generator_fn> generators_;
};

} // namespace turbostar
