#pragma once
#include <string>
#include "../../agentlib/llm_tool_action.h"
#include "../../agentlib/single_file_tool_validator.h"

namespace tools
{

class open_in_editor_tool : public agentlib::llm_tool_action
{
      public:
	explicit open_in_editor_tool(std::string safe_path);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	std::string safe_path_;
};

class open_in_editor_validator : public agentlib::single_file_tool_validator
{
      public:
	std::string get_name() const override
	{
		return "open_in_editor";
	}
	std::string get_description() const override
	{
		return "Open a file in the editor UI for the user to view or edit.";
	}
	std::string get_parameter_name() const override
	{
		return "filename";
	}
	std::string get_parameter_description() const override
	{
		return "The path of the file to open in the editor.";
	}

	agentlib::access_type get_required_permission() const override
	{
		return agentlib::access_type::read;
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_from_resolved_path(const std::string &safe_path) const override;
};

} // namespace tools
