#include <nlohmann/json.hpp>
#include "agentlib/tool_registry.h"
#include "agentlib/tool_validator.h"
#include "crashdump_clear.h"

namespace tools
{

class crashdump_clear_validator : public agentlib::tool_validator
{
      public:
	// Impure Domain 3 (Editor & System State): Deletes crash dump files from disk and clears state.
	bool is_pure() const override
	{
		return false;
	}

	std::string get_name() const override
	{
		return "crashdump_clear";
	}
	std::string get_description() const override
	{
		return "Deletes all crash dumps from the disk and clears the internal crash dump list. Use this to remove stale "
		       "crash dumps after they have been investigated.";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {
		    {"type", "object"},
		    {"properties",
		     {{"crash_id",
		       {{"type", "string"},
			{"description", "Optional unique crash identifier (e.g. '12345'). If omitted, all crash dumps are cleared."}}}}}};
	}

	std::unordered_map<std::string, std::string> get_custom_parameter_aliases() const override
	{
		return {{"id", "crash_id"}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json & /*raw_json*/, const agentlib::tool_context & /*ctx*/,
				std::string & /*out_error*/) const override
	{
		return true;
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json &raw_json) const override
	{
		std::string crash_id;
		if (raw_json.contains("crash_id") && raw_json["crash_id"].is_string()) {
			crash_id = raw_json["crash_id"].get<std::string>();
		}
		return std::make_unique<crashdump_clear_tool>(std::move(crash_id));
	}
};

REGISTER_TOOL(crashdump_clear_validator)

} // namespace tools
