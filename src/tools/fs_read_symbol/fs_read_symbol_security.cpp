#include <nlohmann/json.hpp>
#include "../../agentlib/tool_registry.h"
#include "../../agentlib/tool_validator.h"
#include "fs_utils.h"
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
		return "Read the full definition of a function, method, class, struct, or variable by name from a file. Use this to inspect a specific symbol's implementation without guessing line numbers or reading full files.";;
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

		if (canonical_path.find("://") == std::string::npos) {
			std::error_code ec;
			auto st = std::filesystem::status(canonical_path, ec);
			if (!std::filesystem::exists(st)) {
				out_error = "File does not exist: " + path_arg;
				return false;
			}
			if (std::filesystem::is_directory(st)) {
				out_error = "Path is a directory, not a regular file: " + path_arg;
				return false;
			}
			if (!std::filesystem::is_regular_file(st)) {
				out_error = "Target is not a regular file (e.g. FIFO/device): " + path_arg;
				return false;
			}
		}

		if (symbol_name_arg.length() > 256) {
			out_error = "symbol_name exceeds maximum length of 256 characters.";
			return false;
		}
		if (!fs_utils::is_safe_for_ui(symbol_name_arg)) {
			out_error = "Security Violation: symbol_name contains unsafe control characters.";
			return false;
		}

		args_.requested_path = path_arg;
		args_.safe_path = canonical_path;
		args_.symbol_name = symbol_name_arg;

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
