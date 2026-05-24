#include <nlohmann/json.hpp>
#include "../../agentlib/tool_registry.h"
#include "../../agentlib/tool_validator.h"
#include "fs_regexp_lines.h"

namespace tools
{

struct fs_regexp_lines_raw_args {
	std::string path;
	std::string pattern;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(fs_regexp_lines_raw_args, path, pattern);

class fs_regexp_lines_validator : public agentlib::tool_validator
{
      public:
	std::string get_name() const override
	{
		return "fs_regexp_lines";
	}
	std::string get_description() const override
	{
		return "Search for a regular expression within a file and return matching lines as a Markdown table.";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {{"type", "object"},
			{"properties",
			 {{"path", {{"type", "string"}, {"description", "The path to the file, relative to the project root."}}},
			  {"pattern",
			   {{"type", "string"}, {"description", "The C++ std::regex pattern to search for (e.g., 'function.*foo')."}}}}},
			{"required", nlohmann::json::array({"path", "pattern"})}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx, std::string &out_error) const override
	{
		try {
			fs_regexp_lines_raw_args parsed = raw_json.get<fs_regexp_lines_raw_args>();

			if (parsed.path.empty() || parsed.pattern.empty()) {
				out_error = "Path and pattern parameters cannot be empty.";
				return false;
			}

			std::string canonical_path;
			if (!ctx.fs_security.validate_access(parsed.path, agentlib::access_type::read, canonical_path, out_error)) {
				return false;
			}

			args_.path = parsed.path;
			args_.pattern = parsed.pattern;
			args_.safe_path = canonical_path;

			return true;
		} catch (const std::exception &e) {
			out_error = "Invalid arguments: " + std::string(e.what());
			return false;
		}
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json & /*raw_json*/) const override
	{
		return std::make_unique<fs_regexp_lines_tool>(args_);
	}

      private:
	mutable fs_regexp_lines_args args_;
};

REGISTER_TOOL(fs_regexp_lines_validator)

} // namespace tools
