#pragma once

#include <memory>
#include <string>
#include <vector>
#include "mcp_server.h"

namespace agentlib
{

class mcp_manager
{
      public:
	static mcp_manager &get_instance();

	void discover_and_load(const std::string &project_root = "");
	const std::vector<std::shared_ptr<mcp_server>> &get_servers() const
	{
		return servers_;
	}
	std::shared_ptr<mcp_server> find_server(const std::string &name) const;
	void save_configs(const std::string &project_root = "");

	// Dynamic lifecycle control
	void start_active_servers();
	void stop_all_servers();
	void toggle_server(const std::string &name, bool enable);
	void toggle_tool(const std::string &server_name, const std::string &tool_name, bool enable);

      private:
	mcp_manager() = default;
	void load_servers_from_file(const std::string &path, bool is_system);

	std::vector<std::shared_ptr<mcp_server>> servers_;
};

} // namespace agentlib
