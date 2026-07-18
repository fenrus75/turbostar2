#include <nlohmann/json.hpp>
#include "../../agentlib/tool_registry.h"
#include "../../agentlib/tool_validator.h"
#include "fs_read_symbol.h"

namespace tools
{

class fs_read_symbol_validator : public agentlib::tool_validator
{
      public:
	bool is_pure() const override
	{
		return true;
	}
	bool is_silent_by_default() const override
	{
		return false;
	}

	std::string get_name() const override
	{
		return "fs_read_symbol";
	}
	std::string get_description() const override
	{
		return "Read the full definition of a function, method, class, struct, or variable by name from a file. Uses LSP to find the exact boundaries.";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {{"type", "object"},
			{"properties",
			 {{"path", {{"type", "string"}, {"description", "The path to the file, relative to the project root."}}},
			  {"symbol_name",
			   {{"type", "string"},
			    {"description", "The name of the function, method, class, struct, or variable to read. Supports namespace/class scopes (e.g. Class::method)."}}}}},
			{"required", nlohmann::json::array({"path", "symbol_name"})}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx, std::string &out_error) const override
	{
		if (!raw_json.contains("path") || !raw_json["path"].is_string()) {
			out_error = "Missing or invalid 'path' string parameter.";
			return false;
		}
		if (!raw_json.contains("symbol_name") || !raw_json["symbol_name"].is_string()) {
			out_error = "Missing or invalid 'symbol_name' string parameter.";
			return false;
		}

		std::string path_arg = raw_json["path"].get<std::string>();
		std::string symbol_name_arg = raw_json["symbol_name"].get<std::string>();

		if (path_arg.empty()) {
			out_error = "Path parameter cannot be empty.";
			return false;
		}
		if (symbol_name_arg.empty()) {
			out_error = "Symbol name parameter cannot be empty.";
			return false;
		}

		std::string canonical_path;
		if (!ctx.fs_security.validate_access(path_arg, agentlib::access_type::read, canonical_path, out_error)) {
			return false;
		}

		args_.requested_path = path_arg;
		args_.symbol_name = symbol_name_arg;
		args_.safe_path = canonical_path;

		return true;
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json & /*raw_json*/) const override
	{
		return std::make_unique<fs_read_symbol_tool>(args_);
	}

      private:
	mutable fs_read_symbol_args args_;
};

REGISTER_TOOL(fs_read_symbol_validator)

} // namespace tools
