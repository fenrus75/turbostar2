#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "mcp_server.h"

namespace agentlib
{

class mcp_manager
{
      public:
	static mcp_manager &get_instance();

	void discover_and_load(const std::string &project_root = "");
	std::vector<std::shared_ptr<mcp_server>> get_servers() const;
	std::shared_ptr<mcp_server> find_server(const std::string &name) const;
	void save_configs(const std::string &project_root = "");

	// Dynamic lifecycle control
	void start_async(const std::string &project_root);
	void start_active_servers();
	void stop_all_servers();
	void toggle_server(const std::string &name, bool enable);
	void toggle_tool(const std::string &server_name, const std::string &tool_name, bool enable);

	void queue_prompt_generation(const std::string &server_name, bool is_system);

      private:
	mcp_manager() = default;
	~mcp_manager();
	void load_servers_from_file(const std::string &path, bool is_system);
	void prompt_worker_loop();

	std::vector<std::shared_ptr<mcp_server>> servers_;
	mutable std::recursive_mutex mutex_;
	std::thread startup_thread_;

	std::vector<std::pair<std::string, bool>> prompt_generation_queue_;
	std::thread prompt_generation_thread_;
	std::mutex prompt_mutex_;
	std::condition_variable prompt_cv_;
	std::atomic<bool> prompt_thread_running_{false};
};

} // namespace agentlib
