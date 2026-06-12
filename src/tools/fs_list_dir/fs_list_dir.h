#pragma once
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include "../../agentlib/llm_tool_action.h"
#include "../../agentlib/tool_validator.h"

namespace tools
{

class fs_list_dir_tool : public agentlib::llm_tool_action
{
      public:
	explicit fs_list_dir_tool(std::string safe_path, bool rich_metadata = false);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	std::string safe_path_;
	bool rich_metadata_;
};

class fs_list_dir_validator : public agentlib::tool_validator
{
      public:
	std::string get_name() const override
	{
		return "fs_list_dir";
	}
	std::string get_description() const override
	{
		return "List the contents of a directory as a Markdown table.";
	}
	nlohmann::json get_parameters_schema() const override;

	bool is_pure() const override
	{
		return true;
	}

      protected:
	bool validate_args_impl(const nlohmann::json &args, const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json &args) const override;

      private:
	mutable std::string resolved_path_;
	mutable bool rich_metadata_{false};
};

} // namespace tools
