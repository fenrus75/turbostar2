#include "../../agentlib/tool_registry.h"
#include "perform_code_review.h"

namespace tools
{

struct perform_code_review_raw_args {
	std::vector<std::string> files;
	std::string instructions;
	std::string result_file;
	bool async = false;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(perform_code_review_raw_args, files, instructions, result_file, async);

bool perform_code_review_validator::validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx,
						       std::string &out_error) const
{
	try {
		perform_code_review_raw_args parsed = raw_json.get<perform_code_review_raw_args>();

		if (parsed.files.empty()) {
			out_error = "Files array must not be empty.";
			return false;
		}

		// Enforce path access at validation time instead of letting the execute path
		// silently start reviewer agents on files the agent cannot read (or write a
		// result report it cannot write). This mirrors the fs security manager used
		// by the single-file tools.
		for (const auto &f : parsed.files) {
			std::string resolved;
			std::string access_err;
			if (!ctx.fs_security.validate_access(f, agentlib::access_type::read, resolved, access_err)) {
				out_error = "Error: No read access to file '" + f + "': " + access_err;
				return false;
			}
		}
		if (!parsed.result_file.empty()) {
			std::string resolved;
			std::string access_err;
			if (!ctx.fs_security.validate_access(parsed.result_file, agentlib::access_type::write, resolved, access_err)) {
				out_error = "Error: No write access to result_file '" + parsed.result_file + "': " + access_err;
				return false;
			}
		}

		args_.files = parsed.files;
		args_.instructions = parsed.instructions;
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
