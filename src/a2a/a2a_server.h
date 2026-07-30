#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace httplib
{
class Server;
}

namespace a2a
{

struct a2a_task_info {
	std::string id;
	std::string agent_name;
	std::string status; // "enqueued", "running", "completed", "failed", "cancelled"
	nlohmann::json input_params;
	nlohmann::json output_result;
	std::string error_message;
	int progress_percent = 0;
	std::string created_at;
	std::string updated_at;
};

class a2a_server
{
      public:
	static a2a_server &get_instance();

	/*
	 * Starts the HTTP A2A server in a background worker thread.
	 * Returns true if server successfully bound and started listening.
	 * out_bound_port returns the actual port bound (e.g. 7820 or incremented fallback).
	 */
	bool start(int base_port = 7820, int *out_bound_port = nullptr);

	/*
	 * Stops the server background thread.
	 */
	void stop();

	/*
	 * Returns true if server thread is actively listening.
	 */
	bool is_running() const;

	/*
	 * Gets the actual port number bound by the server.
	 */
	int get_bound_port() const;

	/*
	 * Model configuration API
	 */
	void set_default_model(const std::string &model);
	std::string get_default_model() const;

	/*
	 * Git worktree configuration API
	 */
	void set_use_git_worktree(bool enable);
	bool is_use_git_worktree() const;

	/*
	 * Task management API
	 */
	std::string create_task(const std::string &agent_name, const nlohmann::json &input_params, std::string &out_error);
	std::optional<a2a_task_info> get_task(const std::string &task_id) const;
	bool cancel_task(const std::string &task_id);

      private:
	a2a_server();
	~a2a_server();

	void setup_routes();

	std::unique_ptr<httplib::Server> server_;
	std::thread server_thread_;
	std::atomic<bool> running_{false};
	std::atomic<int> bound_port_{0};
	std::atomic<bool> use_git_worktree_{false};
	std::string default_model_;

	/*
	 * tasks_mutex_ protects access to the tasks_ unordered_map when creating,
	 * querying, or updating task statuses across HTTP worker threads and execution handlers.
	 * Callers must hold tasks_mutex_ whenever inspecting or modifying tasks_.
	 */
	mutable std::mutex tasks_mutex_;
	std::unordered_map<std::string, a2a_task_info> tasks_;
};

} // namespace a2a
