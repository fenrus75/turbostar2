#include "agentlib/tool_registry.h"
#include "agentlib/tool_validator.h"
#include "plugins/sqlite/sqlite_list_db.h"

namespace tools
{

class sqlite_list_db_validator : public agentlib::tool_validator
{
      public:
	std::string get_family() const override
	{
		return "sqlite";
	}

	bool is_pure() const override
	{
		return true;
	}

	std::string get_name() const override
	{
		return "sqlite_list_db";
	}
	std::string get_description() const override
	{
		return "Lists all persistent SQLite databases available for the project.";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {{"type", "object"}, {"properties", nlohmann::json::object()}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json & /*args*/, const agentlib::tool_context & /*ctx*/,
				std::string & /*out_error*/) const override
	{
		return true;
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json & /*args*/) const override
	{
		return std::make_unique<sqlite_list_db_tool>();
	}
};

} // namespace tools

extern "C" {
void register_sqlite_list_db(void)
{
	agentlib::tool_registry::get_instance().register_validator([]() { return std::make_unique<tools::sqlite_list_db_validator>(); });
}

void unregister_sqlite_list_db(void)
{
	agentlib::tool_registry::get_instance().unregister_validator("sqlite_list_db");
}
}
