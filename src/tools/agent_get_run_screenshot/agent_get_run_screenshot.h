#pragma once

#include "agentlib/llm_tool_action.h"
#include "agentlib/tool_context.h"
#include "agentlib/tool_validator.h"
#include <nlohmann/json.hpp>
#include <string>

namespace tools
{

struct agent_get_run_screenshot_args {
	int run_id{-1};
	bool settle{false};
	std::string tool_name{"agent_get_run_terminaldump"};
};

class agent_get_run_screenshot_tool : public agentlib::llm_tool_action
{
      public:
	explicit agent_get_run_screenshot_tool(agent_get_run_screenshot_args args)
	    : llm_tool_action("Capturing application/debugger terminal dump"), args_(std::move(args))
	{
	}

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	agent_get_run_screenshot_args args_;
};

using agent_get_run_terminaldump_tool = agent_get_run_screenshot_tool;

/*

# subclasses of agent_get_run_terminaldump_validator

| subclass                            | filename                                                       |
| ----------------------------------- | -------------------------------------------------------------- | 
| agent_get_run_screenshot_validator   | src/tools/agent_get_run_screenshot/agent_get_run_screenshot.h |
| agent_run_get_terminaldump_validator | src/tools/agent_get_run_screenshot/agent_get_run_screenshot.h |
| agent_run_get_screenshot_validator   | src/tools/agent_get_run_screenshot/agent_get_run_screenshot.h |

*/
class agent_get_run_terminaldump_validator : public agentlib::tool_validator
{
      public:
	bool is_pure() const override
	{
		return true;
	}

	std::string get_name() const override
	{
		return "agent_get_run_terminaldump";
	}
	std::string get_description() const override
	{
		return "Returns an ASCII terminal dump of the screen buffer, cursor coordinates, and process alive status for a given run ID formatted as Markdown.";
	}

	nlohmann::json get_parameters_schema() const override;

      protected:
	bool validate_args_impl(const nlohmann::json &args_json, const agentlib::tool_context &ctx,
				std::string &out_error) const override;

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json &args) const override;

	mutable agent_get_run_screenshot_args args_;
};

class agent_get_run_screenshot_validator : public agent_get_run_terminaldump_validator
{
      public:
	std::string get_name() const override
	{
		return "agent_get_run_screenshot";
	}
	std::string get_description() const override
	{
		return "Alias for agent_get_run_terminaldump: Returns an ASCII terminal dump of the screen buffer for a given run ID.";
	}
};

class agent_run_get_terminaldump_validator : public agent_get_run_terminaldump_validator
{
      public:
	std::string get_name() const override
	{
		return "agent_run_get_terminaldump";
	}
	std::string get_description() const override
	{
		return "Alias for agent_get_run_terminaldump: Returns an ASCII terminal dump of the screen buffer for a given run ID.";
	}
};

class agent_run_get_screenshot_validator : public agent_get_run_terminaldump_validator
{
      public:
	std::string get_name() const override
	{
		return "agent_run_get_screenshot";
	}
	std::string get_description() const override
	{
		return "Alias for agent_get_run_terminaldump: Returns an ASCII terminal dump of the screen buffer for a given run ID.";
	}
};

} // namespace tools

