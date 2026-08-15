#include <nlohmann/json.hpp>
#include <optional>
#include "agentlib/tool_registry.h"
#include "agentlib/tool_validator.h"
#include "ask_user.h"
#include "fs_utils.h"

namespace tools
{

struct ask_user_raw_args {
	std::vector<nlohmann::json> questions;
};

// We only need the top-level "questions" array for now based on typical ask_user schema
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ask_user_raw_args, questions);

class ask_user_validator : public agentlib::tool_validator
{
      public:
	// Pure Domain 2 (Agent & Workflow State): Prompts user for interactive input during agent execution.
	bool is_pure() const override
	{
		return true;
	}

	std::string get_name() const override
	{
		return "ask_user";
	}
	std::string get_description() const override
	{
		return "Ask the user a question to gather preferences or make decisions. A custom input 'Other' field is automatically "
		       "provided.";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {{"type", "object"},
			{"properties",
			 {{"questions",
			   {{"type", "array"},
			    {"items",
			     {{"type", "object"},
			      {"properties",
			       {{"question", {{"type", "string"}, {"description", "The complete question to ask the user."}}},
				{"options",
				 {{"type", "array"},
				  {"items", {{"type", "string"}}},
				  {"description", "Selectable choices. An 'Other' option is automatically added."}}}}},
			      {"required", nlohmann::json::array({"question"})}}}}}}},
			{"required", nlohmann::json::array({"questions"})}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context & /*ctx*/,
				std::string &out_error) const override
	{
		try {
			ask_user_raw_args parsed = raw_json.get<ask_user_raw_args>();

			if (parsed.questions.empty()) {
				out_error = "Must provide at least one question.";
				return false;
			}

			if (parsed.questions.size() > 1) {
				out_error = "Validation Error: Multiple questions in a single ask_user call are not supported.";
				return false;
			}

			auto &q = parsed.questions[0];
			if (!q.contains("question") || !q["question"].is_string()) {
				out_error = "Question must have a 'question' string.";
				return false;
			}

			std::string q_str = q["question"].get<std::string>();
			if (q_str.empty()) {
				out_error = "Validation Error: Question string cannot be empty.";
				return false;
			}
			if (q_str.length() > 1000) {
				out_error = "Validation Error: Question string exceeds maximum length limit of 1000 characters.";
				return false;
			}
			if (!fs_utils::is_safe_for_ui(q_str)) {
				out_error = "Validation Error: Question contains unsafe control or ANSI escape characters.";
				return false;
			}

			args_.question = q_str;
			if (q.contains("options") && q["options"].is_array()) {
				if (q["options"].size() > 10) {
					out_error = "Validation Error: Exceeded maximum option limit of 10 options.";
					return false;
				}
				for (const auto &opt : q["options"]) {
					std::string opt_str;
					if (opt.is_string()) {
						opt_str = opt.get<std::string>();
					} else if (opt.is_object() && opt.contains("label") && opt["label"].is_string()) {
						opt_str = opt["label"].get<std::string>();
					}
					if (!opt_str.empty()) {
						if (opt_str.length() > 100) {
							opt_str = opt_str.substr(0, 97) + "...";
						}
						if (fs_utils::is_safe_for_ui(opt_str)) {
							args_.options.push_back(opt_str);
						}
					}
				}
			}
			return true;
		} catch (const std::exception &e) {
			out_error = "Invalid arguments: " + std::string(e.what());
			return false;
		}
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json & /*raw_json*/) const override
	{
		return std::make_unique<ask_user_tool>(args_);
	}

      private:
	mutable ask_user_args args_;
};

REGISTER_TOOL(ask_user_validator)

} // namespace tools
