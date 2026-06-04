#pragma once
#include <memory>
#include <string>
#include "../../agentlib/tool_validator.h"

namespace tools
{

class web_fetch_tool : public agentlib::llm_tool
{
      public:
	explicit web_fetch_tool(std::string url, bool no_ask = false);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	std::string url_;
	std::string domain_;
	bool no_ask_{false};
};

class web_fetch_validator : public agentlib::tool_validator
{
      public:
	std::string get_name() const override
	{
		return "web_fetch";
	}
	std::string get_description() const override
	{
		return "Fetches content from a URL via HTTP/HTTPS. Useful for reading documentation or external resources.";
	}
	nlohmann::json get_parameters_schema() const override;
	bool is_pure() const override
	{
		return true;
	}

      protected:
	bool validate_args_impl(const nlohmann::json &args, const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json &args) const override;
};

} // namespace tools