#include "../../agentlib/tool_registry.h"
#include "perform_code_review.h"

namespace tools
{

struct perform_code_review_raw_args {
	std::vector<std::string> files;
	std::string instructions;
	std::vector<std::string> todos;
	std::string result_file;
	bool async = false;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(perform_code_review_raw_args, files, instructions, todos, result_file, async);

bool perform_code_review_validator::validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context & /*ctx*/,
						       std::string &out_error) const
{
	try {
		perform_code_review_raw_args parsed = raw_json.get<perform_code_review_raw_args>();

		if (parsed.files.empty()) {
			out_error = "Files array must not be empty.";
			return false;
		}

		args_.files = parsed.files;
		args_.instructions = parsed.instructions;
		args_.todos = parsed.todos;
		args_.result_file = parsed.result_file;
		args_.async = parsed.async;
		return true;
	} catch (const std::exception &e) {
		out_error = "Invalid arguments: " + std::string(e.what());
		return false;
	}
}

std::unique_ptr<agentlib::llm_tool> perform_code_review_validator::create_tool_impl(const nlohmann::json & /*raw_json*/) const
{
	return std::make_unique<perform_code_review_tool>(args_);
}

REGISTER_TOOL(perform_code_review_validator)

} // namespace tools
