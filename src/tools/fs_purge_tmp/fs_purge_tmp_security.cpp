#include "../../agentlib/tool_registry.h"
#include "../../agentlib/tool_validator.h"
#include "fs_purge_tmp.h"

namespace tools
{

class fs_purge_tmp_validator : public agentlib::tool_validator
{
      public:
	bool is_pure() const override
	{
		return false;
	}

	std::string get_name() const override
	{
		return "fs_purge_tmp";
	}
	std::string get_description() const override
	{
		return "Purges (deletes) files and directories in the virtual tmp:// scratch space. "
		       "If a substring is provided, only deletes files/directories whose names contain the substring.";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {{"type", "object"},
			{"properties",
			 {{"substring",
			   {{"type", "string"},
			    {"description", "Optional. Only delete files containing this substring in their name/path."}}}}}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &args, const agentlib::tool_context &ctx,
				std::string &out_error) const override
	{
		std::string resolved_path;
		if (!ctx.fs_security.validate_access("tmp://", agentlib::access_type::write, resolved_path, out_error)) {
			return false;
		}
		if (args.contains("substring") && !args["substring"].is_string()) {
			out_error = "Parameter 'substring' must be a string.";
			return false;
		}
		return true;
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json &args) const override
	{
		std::string substring;
		if (args.contains("substring")) {
			substring = args["substring"].get<std::string>();
		}
		return std::make_unique<fs_purge_tmp_tool>(std::move(substring));
	}
};

REGISTER_TOOL(fs_purge_tmp_validator)

} // namespace tools
