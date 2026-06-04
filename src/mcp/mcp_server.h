#pragma once

#include <atomic>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <vector>
#include "../agentlib/tool_validator.h"

namespace agentlib
{

struct mcp_tool {
	std::string name;
	std::string description;
	std::string schema_json;
	bool enabled{true};
};

class mcp_server
{
      public:
	mcp_server() = default;
	~mcp_server();

	std::string get_name() const
	{
		return name_;
	}
	void set_name(const std::string &name)
	{
		name_ = name;
	}

	std::string get_command() const
	{
		return command_;
	}
	void set_command(const std::string &cmd)
	{
		command_ = cmd;
	}

	std::vector<std::string> get_args() const
	{
		return args_;
	}
	void set_args(const std::vector<std::string> &args)
	{
		args_ = args;
	}

	std::map<std::string, std::string> get_env() const
	{
		return env_;
	}
	void set_env(const std::map<std::string, std::string> &env)
	{
		env_ = env;
	}

	bool is_system() const
	{
		return is_system_;
	}
	void set_system(bool is_sys)
	{
		is_system_ = is_sys;
	}

	bool is_enabled() const
	{
		return enabled_;
	}
	void set_enabled(bool enabled)
	{
		enabled_ = enabled;
	}

	std::string get_mcp_type() const
	{
		return mcp_type_;
	}
	void set_mcp_type(const std::string &type)
	{
		mcp_type_ = type;
	}

	// Note: get_tools() returns a copy of tools_ and does NOT acquire mutex_
	// This is safe because tools_ is only written during initialization in start()
	// after the MCP handshake completes, and is read-only afterwards.
	// If tools_ needs to be modified at runtime, this method must acquire mutex_.
	std::vector<mcp_tool> get_tools() const
	{
		return tools_;
	}
	// Note: set_tools() does NOT acquire mutex_. Callers must ensure thread-safety.
	// Currently only called from mcp_manager::toggle_tool() which already holds mutex_.
	void set_tools(const std::vector<mcp_tool> &tools)
	{
		tools_ = tools;
	}

	void auto_detect_type();
	std::string get_python_script_path() const;
	bool run_bandit_scan(std::string &scan_output) const;

	// Lifecycle management
	bool start();
	void stop();
	bool is_running() const
	{
		return pid_ > 0;
	}

	// Communication
	nlohmann::json send_request(const std::string &method, const nlohmann::json &params);
	void send_notification(const std::string &method, const nlohmann::json &params);

      private:
	std::string name_;
	std::string command_;
	std::vector<std::string> args_;
	std::map<std::string, std::string> env_;
	bool is_system_{false};
	bool enabled_{true};
	std::string mcp_type_{"other"};
	std::vector<mcp_tool> tools_;

	// Subprocess details
	pid_t pid_{0};
	int stdin_fd_{-1};
	int stdout_fd_{-1};
	int stderr_fd_{-1};
	std::atomic<bool> reader_running_{false};
	std::thread reader_thread_;
	std::thread stderr_thread_;

	std::mutex state_mutex_;

	// JSON-RPC Request Matcher
	std::mutex requests_mutex_;
	int next_request_id_{1};
	std::map<int, std::promise<nlohmann::json>> pending_requests_;

	void reader_loop();
	void stderr_loop();
};

class mcp_tool_validator : public tool_validator
{
      public:
	mcp_tool_validator(const std::string &server_name, const mcp_tool &tool, bool is_system)
	    : server_name_(server_name), tool_(tool), is_system_(is_system)
	{
	}

	std::string get_name() const override
	{
		return "mcp:" + server_name_ + ":" + tool_.name;
	}
	std::string get_description() const override
	{
		return tool_.description;
	}
	nlohmann::json get_parameters_schema() const override
	{
		if (tool_.schema_json.empty()) {
			return nlohmann::json::object();
		}
		return nlohmann::json::parse(tool_.schema_json);
	}
	bool is_pure() const override
	{
		return false;
	}

      protected:
	bool validate_args_impl(const nlohmann::json &, const tool_context &, std::string &) const override
	{
		return true;
	}
	std::unique_ptr<llm_tool> create_tool_impl(const nlohmann::json &args) const override;

      private:
	std::string server_name_;
	mcp_tool tool_;
	bool is_system_;
};

class mcp_llm_tool : public llm_tool
{
      public:
	mcp_llm_tool(const std::string &server_name, const std::string &tool_name, const nlohmann::json &args)
	    : server_name_(server_name), tool_name_(tool_name), args_(args)
	{
	}

	bool validate_runtime(const tool_context &, std::string &) const override
	{
		return true;
	}
	std::string execute(tool_context &ctx) override;

      private:
	std::string server_name_;
	std::string tool_name_;
	nlohmann::json args_;
};

} // namespace agentlib
