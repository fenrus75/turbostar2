#include <nlohmann/json.hpp>
#include <optional>
#include "../../agentlib/tool_registry.h"
#include "../../agentlib/tool_validator.h"
#include "fs_read_lines.h"

namespace tools
{

// A struct to deserialize the JSON arguments into, before validation.
struct fs_read_lines_raw_args {
	std::string path;
	int start_line = -1;
	int end_line = -1;
};

// Map JSON to the raw struct
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(fs_read_lines_raw_args, path, start_line, end_line);

class fs_read_lines_validator : public agentlib::tool_validator
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
		return "fs_read_lines";
	}
	std::string get_description() const override
	{
		return "Reads a specific range of text lines from a file. Output lines are prefixed with their 1-based line number in '<line_number>: <line_text>' format.";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {
		    {"type", "object"},
		    {"properties",
		     {{"path", {{"type", "string"}, {"description", "The path to the file, relative to the project root."}}},
		      {"start_line",
		       {{"type", "integer"}, {"description", "The 1-based line number to start reading from. Defaults to 1 if omitted."}}},
		      {"end_line",
		       {{"type", "integer"},
			{"description", "The 1-based line number to end reading at (inclusive)."}}}}},
		    {"required", nlohmann::json::array({"path"})}};
	}

      protected:
	// Stage 1: Pre-invocation validation
	bool validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx, std::string &out_error) const override
	{
		try {
			fs_read_lines_raw_args parsed = raw_json.get<fs_read_lines_raw_args>();

			if (parsed.path.empty()) {
				out_error = "Path parameter cannot be empty.";
				return false;
			}

			// CRITICAL: Perform the file security manager check (access_type::read)
			std::string canonical_path;
			if (!ctx.fs_security.validate_access(parsed.path, agentlib::access_type::read, canonical_path, out_error)) {
				return false;
			}

			// Populate the strict args for the tool
			args_.requested_path = parsed.path;
			args_.safe_path = canonical_path;
			args_.start_line = (parsed.start_line == -1) ? 1 : parsed.start_line;
			args_.end_line = (parsed.end_line == -1) ? 1000000 : parsed.end_line;

			if (args_.start_line < 1)
				args_.start_line = 1;
			if (args_.end_line < args_.start_line) {
				out_error = "end_line cannot be less than start_line.";
				return false;
			}

			return true;
		} catch (const std::exception &e) {
			out_error = "Invalid arguments: " + std::string(e.what());
			return false;
		}
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json & /*raw_json*/) const override
	{
		// Because tool_validators are transient (created per-request in tool_registry::execute_tool),
		// it is perfectly thread-safe to store the validated args in a mutable member variable
		// during validate_args_impl and consume them here.
		return std::make_unique<fs_read_lines_tool>(args_);
	}

      private:
	mutable fs_read_lines_args args_;
};

// Register the tool with the global registry
REGISTER_TOOL(fs_read_lines_validator)

} // namespace tools
