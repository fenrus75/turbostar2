#pragma once
#include <map>
#include <memory>
#include <string>
#include "../../agentlib/tool_validator.h"

namespace tools
{

struct web_fetch_args {
	std::string url;
	std::string method = "GET";
	std::map<std::string, std::string> headers;
	std::string output_path;
	std::string safe_output_path;
	std::string filter;
	bool no_ask = false;
};


class web_fetch_tool : public agentlib::llm_tool
{
      public:
	explicit web_fetch_tool(web_fetch_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	web_fetch_args args_;
	std::string domain_;
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
		return "Fetches content from a URL via HTTP/HTTPS. Useful for reading documentation or external resources. Implements domain-based access controls and prompts the user for permission.";;
	}
	nlohmann::json get_parameters_schema() const override;
	std::vector<agentlib::tool_example> get_examples() const override;
	bool is_pure() const override

	{
		return true;
	}

      protected:
	bool validate_args_impl(const nlohmann::json &args, const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json &args) const override;

      private:
	mutable web_fetch_args parsed_args_;
};

} // namespace tools