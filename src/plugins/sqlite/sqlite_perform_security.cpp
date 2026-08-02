#include "agentlib/tool_registry.h"
#include "agentlib/tool_validator.h"
#include "fs_utils.h"
#include "plugins/sqlite/sqlite_perform.h"
#include <cctype>
#include <string>
#include <vector>

namespace tools
{

static bool validate_query_safety(const std::string &query, std::string &out_error)
{
	size_t start = query.find_first_not_of(" \t\n\r");
	if (start == std::string::npos) {
		out_error = "Query cannot be empty or whitespace-only.";
		return false;
	}

	std::string trimmed = query.substr(start);

	size_t semicolon_pos = trimmed.find(';');
	if (semicolon_pos != std::string::npos) {
		if (semicolon_pos != trimmed.size() - 1) {
			out_error = "Multi-statement queries are not allowed. Only a single SQL statement is permitted.";
			return false;
		}
		trimmed = trimmed.substr(0, trimmed.size() - 1);
	}

	std::string upper_query;
	upper_query.reserve(trimmed.size());
	for (char c : trimmed) {
		upper_query += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	}

	if (upper_query.find("ATTACH") == 0) {
		out_error = "ATTACH statements are not allowed for security reasons.";
		return false;
	}

	return true;
}

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
