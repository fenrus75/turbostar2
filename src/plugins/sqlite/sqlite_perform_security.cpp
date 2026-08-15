#include "agentlib/tool_registry.h"
#include "agentlib/tool_validator.h"
#include "fs_utils.h"
#include "plugins/sqlite/sqlite_perform.h"
#include "plugins/sqlite/sqlite_query_validation.h"

namespace tools
{

class sqlite_perform_validator : public agentlib::tool_validator
{
      public:
	std::string get_family() const override
	{
		return "sqlite";
	}
	std::string get_name() const override
	{
		return "sqlite_perform";
	}
	std::string get_description() const override
	{
		return "Executes arbitrary SQL queries on a persistent SQLite database.";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {{"type", "object"},
			{"properties",
			 {{"database", {{"type", "string"}, {"description", "The simple name of the database to query."}}},
			  {"query", {{"type", "string"}, {"description", "The SQL command(s) to execute."}}}}},
			{"required", nlohmann::json::array({"database", "query"})}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &args, const agentlib::tool_context & /*ctx*/, std::string &out_error) const override
	{
		if (!args.contains("database") || !args["database"].is_string()) {
			out_error = "Missing or invalid 'database' parameter.";
			return false;
		}
		if (!args.contains("query") || !args["query"].is_string()) {
			out_error = "Missing or invalid 'query' parameter.";
			return false;
		}

		std::string db = args["database"].get<std::string>();
		if (!fs_utils::is_valid_db_name(db)) {
			out_error = "Database name must contain only a-z, A-Z, 0-9, _, and -.";
			return false;
		}

		std::string query = args["query"].get<std::string>();
		if (!validate_query_safety(query, out_error)) {
			return false;
		}

		return true;
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json &args) const override
	{
		return std::make_unique<sqlite_perform_tool>(args["database"].get<std::string>(), args["query"].get<std::string>());
	}
};

} // namespace tools

extern "C" {
void register_sqlite_perform(void)
{
	agentlib::tool_registry::get_instance().register_validator([]() { return std::make_unique<tools::sqlite_perform_validator>(); });
}

void unregister_sqlite_perform(void)
{
	agentlib::tool_registry::get_instance().unregister_validator("sqlite_perform");
}
}
