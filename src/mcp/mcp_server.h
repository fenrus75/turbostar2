#pragma once

#include <map>
#include <string>
#include <vector>

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

	std::vector<mcp_tool> get_tools() const
	{
		return tools_;
	}
	void set_tools(const std::vector<mcp_tool> &tools)
	{
		tools_ = tools;
	}

	void auto_detect_type();

      private:
	std::string name_;
	std::string command_;
	std::vector<std::string> args_;
	std::map<std::string, std::string> env_;
	bool is_system_{false};
	bool enabled_{true};
	std::string mcp_type_{"other"};
	std::vector<mcp_tool> tools_;
};

} // namespace agentlib
