#pragma once
#include "ui/agent_command.h"
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class command_registry
{
      public:
	static command_registry &get_instance();

	void register_command(std::unique_ptr<agent_command> cmd);
	void unregister_command(const std::string &name);

	agent_command *get_command(const std::string &name);
	std::vector<std::string> get_command_names();

      private:
	command_registry();
	~command_registry() = default;
	command_registry(const command_registry &) = delete;
	command_registry &operator=(const command_registry &) = delete;

	/*
	 * Protects the commands_ map and the command_names_ vector.
	 *
	 * Locking rules:
	 * - Must be acquired for any read or write access to commands_ or command_names_.
	 * - Is acquired locally inside register_command, unregister_command, get_command,
	 *   and get_command_names.
	 * - No nested locking is allowed.
	 */
	std::mutex mutex_;

	std::unordered_map<std::string, std::unique_ptr<agent_command>> commands_;
	std::vector<std::string> command_names_;
};
