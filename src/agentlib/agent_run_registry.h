#pragma once

#include <chrono>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace agentlib
{

enum class run_target_kind { app, debugger };

struct run_session_info {
	int run_id{-1};
	run_target_kind kind{run_target_kind::app};
	int linked_run_id{-1};
	std::string binary_path;
	bool is_alive{true};
	bool was_terminated{false};
	std::chrono::steady_clock::time_point created_at{};
};

class agent_run_registry
{
      public:
	agent_run_registry() = default;
	~agent_run_registry() = default;

	// Non-copyable due to mutex
	agent_run_registry(const agent_run_registry &) = delete;
	agent_run_registry &operator=(const agent_run_registry &) = delete;

	// Registration APIs
	void register_app_run(int app_run_id, std::string_view binary = "", int linked_gdb_id = -1);
	void register_gdb_run(int gdb_run_id, int linked_app_id = -1);

	// Teardown tracking APIs
	// Marks the run and any linked run as terminated. Returns all IDs affected (e.g. {app_id, gdb_id}).
	std::vector<int> mark_terminated(int run_id);
	bool is_already_terminated(int run_id) const;
	int get_linked_run_id(int run_id) const;

	// Resolution APIs
	std::optional<int> resolve_run_id(const std::optional<std::string> &untrusted_id,
					  run_target_kind preferred_kind = run_target_kind::app) const;

	// JSON parsing helper for tool validators:
	// Extracts run_id (if present as int or string, or absent) and resolves it via resolve_run_id.
	bool resolve_from_json(const nlohmann::json &untrusted_args, run_target_kind preferred_kind, int &out_run_id,
			       std::string &out_error) const;

	// State Queries
	std::optional<run_session_info> get_run_info(int run_id) const;
	std::optional<int> get_latest_gdb_id(bool only_alive = true) const;
	std::optional<int> get_latest_app_id(bool only_alive = true) const;
	std::optional<int> get_latest_run_id(bool only_alive = true) const;

	size_t size() const;
	void clear();

      private:
	/*
	 * mutex_ protects runs_, history_, and all run state tracking.
	 * Locking Rules:
	 * - Short-lived internal lock acquired during registration, resolution, and queries.
	 * - Must never be held across external I/O, subprocess communication, or tool execution.
	 */
	mutable std::mutex mutex_;
	std::unordered_map<int, run_session_info> runs_;
	std::vector<int> history_;
};

} // namespace agentlib
