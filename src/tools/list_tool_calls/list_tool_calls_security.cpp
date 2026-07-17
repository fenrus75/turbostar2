#include <memory>
#include <nlohmann/json.hpp>
#include "../../agentlib/tool_registry.h"
#include "../../agentlib/tool_validator.h"
#include "list_tool_calls.h"

namespace tools
{

struct list_tool_calls_raw_args {
	std::string search = "";
	bool show_details = false;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(list_tool_calls_raw_args, search, show_details)

class list_tool_calls_validator : public agentlib::tool_validator
{
      public:
	std::string get_name() const override
	{
		return "list_tool_calls";
	}
	std::string get_description() const override
	{
		return "Lists all available LLM tools and their descriptions as a Markdown table. Allows filtering and listing parameter schemas.";
	}
	bool is_pure() const override
	{
		return true;
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {
		    {"type", "object"},
		    {"properties",
		     {{"search", {{"type", "string"}, {"description", "Optional. A case-insensitive substring pattern to filter tools by name."}}},
		      {"show_details", {{"type", "boolean"}, {"description", "Optional. If true, outputs detailed information for each tool's parameters (types, descriptions, and required status). Default is false."}}}}}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context & /*ctx*/,
				std::string &out_error) const override
	{
		try {
			for (auto it = raw_json.begin(); it != raw_json.end(); ++it) {
				if (it.key() != "search" && it.key() != "show_details") {
					out_error = "Unexpected argument: " + it.key();
					return false;
				}
			}
			list_tool_calls_raw_args parsed = raw_json.get<list_tool_calls_raw_args>();
			args_.search = parsed.search;
			args_.show_details = parsed.show_details;
			return true;
		} catch (const std::exception &e) {
			out_error = "Invalid arguments: " + std::string(e.what());
			return false;
		}
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json & /*args*/) const override
	{
		return std::make_unique<list_tool_calls_tool>(args_);
	}

      private:
	mutable list_tool_calls_args args_;
};

REGISTER_TOOL(list_tool_calls_validator)

} // namespace tools