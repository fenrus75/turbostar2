#include <nlohmann/json.hpp>
#include <optional>
#include "../../agentlib/tool_registry.h"
#include "../../agentlib/tool_validator.h"
#include "git_blame.h"

namespace tools
{

struct git_blame_raw_args {
	std::string path;
	int start_line = 0;
	int end_line = 0;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(git_blame_raw_args, path, start_line, end_line);

nlohmann::json git_blame_validator::get_parameters_schema() const
{
	return {
	    {"type", "object"},
	    {"properties",
	     {{"path", {{"type", "string"}, {"description", "The path to the file, relative to the project root."}}},
	      {"start_line", {{"type", "integer"}, {"description", "Optional 1-based start line. Defaults to 1."}}},
	      {"end_line", {{"type", "integer"}, {"description", "Optional 1-based end line. Defaults to the end of the file."}}}}},
	    {"required", nlohmann::json::array({"path"})}};
}

bool git_blame_validator::validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx, std::string &out_error) const
{
	try {
		git_blame_raw_args raw = raw_json.get<git_blame_raw_args>();
		if (raw.path.empty()) {
			out_error = "The 'path' parameter cannot be empty.";
			return false;
		}

		std::string real_check_path = raw.path;
		if (real_check_path.find("file://") == 0) {
			real_check_path = real_check_path.substr(7);
		}

		std::string resolved_path;
		if (!ctx.fs_security.validate_access(real_check_path, agentlib::access_type::read, resolved_path, out_error)) {
			return false;
		}

		parsed_args_.requested_path = raw.path;
		parsed_args_.start_line = raw.start_line;
		parsed_args_.end_line = raw.end_line;
		parsed_args_.safe_path = resolved_path;
		return true;
	} catch (const std::exception &e) {
		out_error = std::string("Failed to parse arguments: ") + e.what();
		return false;
	}
}

std::unique_ptr<agentlib::llm_tool> git_blame_validator::create_tool_impl(const nlohmann::json & /*args*/) const
{
	return std::make_unique<git_blame_tool>(parsed_args_);
}

REGISTER_TOOL(git_blame_validator)

} // namespace tools
