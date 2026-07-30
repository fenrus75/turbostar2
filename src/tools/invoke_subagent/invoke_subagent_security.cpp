#include <memory>
#include <nlohmann/json.hpp>
#include "agentlib/tool_registry.h"
#include "agentlib/tool_validator.h"
#include "agentlib/ai_agent.h"
#include "agentlib/subagent_manager.h"
#include "a2a/a2a_server_manager.h"
#include "fs_utils.h"
#include "invoke_subagent.h"

namespace tools
{

struct invoke_subagent_raw_args {
	std::string name;
	std::string subagent_name;
	std::string profile;
	std::string task;
	bool wait{false};
	bool local_only{false};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(invoke_subagent_raw_args, name, subagent_name, profile, task, wait, local_only);

/**
 * @brief Validator for the invoke_subagent tool, enforcing name uniqueness, lengths, and string safety.
 */
class invoke_subagent_validator : public agentlib::tool_validator
{
      public:
	bool is_pure() const override
	{
		return false;
	} // Modifies agent state

	std::string get_name() const override
	{
		return "invoke_subagent";
	}
	std::string get_description() const override
	{
		std::string base_desc = "Invokes a subagent to delegate tasks to. You must provide a 'subagent_name', or a 'task' (user request), "
		                        "or a 'profile' (system instructions), or a combination of them.";

		const auto &subagents = agentlib::subagent_manager::get_instance().get_subagents();
		if (subagents.empty()) {
			return base_desc;
		}

		nlohmann::json subagents_arr = nlohmann::json::array();
		for (const auto &sa : subagents) {
			subagents_arr.push_back({
				{"name", sa.name},
				{"description", sa.description},
				{"read_only", sa.read_only},
				{"tool_families", sa.tool_families}
			});
		}

		return base_desc + "\n\n<available_subagents>\n" + subagents_arr.dump() + "\n</available_subagents>";
	}

	nlohmann::json get_parameters_schema() const override
	{
		nlohmann::json enum_arr = nlohmann::json::array();
		for (const auto &sa : agentlib::subagent_manager::get_instance().get_subagents()) {
			enum_arr.push_back(sa.name);
		}

		nlohmann::json subagent_name_prop = {
			{"type", "string"},
			{"description", "Optional name of a pre-configured subagent profile to initialize prompt/tools configuration."}
		};
		if (!enum_arr.empty()) {
			subagent_name_prop["enum"] = enum_arr;
		}

		return {
		    {"type", "object"},
		    {"properties",
		     {{"name", {{"type", "string"}, {"description", "A short, descriptive name for the subagent (max 64 chars)."}}},
		      {"subagent_name", subagent_name_prop},
		      {"profile",
		       {{"type", "string"},
			{"description", "System instructions and personality profile for the subagent. Optional if 'subagent_name' or 'task' is "
					"provided (max 10000 chars)."}}},
		      {"task",
		       {{"type", "string"},
			{"description", "The initial task or request for the subagent to perform. Optional if 'profile' or 'subagent_name' is "
					"provided (max 10000 chars)."}}},
		      {"wait",
		       {{"type", "boolean"},
			{"description", "If true, the tool will wait for the subagent to complete its task and will return its "
					"final response directly. Defaults to false (asynchronous)."},
			{"default", false}}},
		      {"local_only",
		       {{"type", "boolean"},
			{"description", "If true, strictly restricts execution to local subagents. Defaults to false."},
			{"default", false}}}}},
		    {"required", nlohmann::json::array({"name"})}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &args_json, const agentlib::tool_context & /*ctx*/,
				std::string &out_error) const override
	{
		try {
			invoke_subagent_raw_args raw_args = args_json.get<invoke_subagent_raw_args>();
			if (raw_args.name.empty()) {
				out_error = "Agent name cannot be empty.";
				return false;
			}
			if (raw_args.name.length() > 64) {
				out_error = "Agent name exceeds maximum length of 64 characters.";
				return false;
			}
			if (raw_args.profile.length() > 10000) {
				out_error = "Profile exceeds maximum length of 10000 characters.";
				return false;
			}
			if (raw_args.task.length() > 10000) {
				out_error = "Task exceeds maximum length of 10000 characters.";
				return false;
			}
			if (!fs_utils::is_safe_for_ui(raw_args.name)) {
				out_error = "Security Violation: Agent name contains unsafe control characters or escape sequences.";
				return false;
			}

			std::string target_subagent = !raw_args.subagent_name.empty() ? raw_args.subagent_name : raw_args.name;
			auto colon_pos = target_subagent.find(':');
			if (colon_pos != std::string::npos) {
				if (raw_args.local_only) {
					out_error = "Execution Error: Cannot invoke remote subagent '" + target_subagent + "' when local_only is true.";
					return false;
				}

				std::string server_name = target_subagent.substr(0, colon_pos);
				auto server_cfg = a2a::a2a_server_manager::get_instance().find_server(server_name);
				if (!server_cfg.has_value()) {
					out_error = "A2A Server '" + server_name + "' not found in server registry.";
					return false;
				}
			} else if (!raw_args.subagent_name.empty()) {
				auto sa = agentlib::subagent_manager::get_instance().find_subagent_by_name(raw_args.subagent_name);
				if (!sa) {
					out_error = "Subagent profile '" + raw_args.subagent_name + "' not found.";
					return false;
				}
			}

			auto is_safe_multiline = [](const std::string &s) {
				for (unsigned char c : s) {
					if (c < 32 && c != 9 && c != 10 && c != 13) return false;
					if (c == 127) return false;
				}
				return true;
			};

			if (!is_safe_multiline(raw_args.profile)) {
				out_error = "Security Violation: Profile contains unsafe control characters or escape sequences.";
				return false;
			}
			if (!is_safe_multiline(raw_args.task)) {
				out_error = "Security Violation: Task contains unsafe control characters or escape sequences.";
				return false;
			}
			if (raw_args.profile.empty() && raw_args.task.empty() && raw_args.subagent_name.empty()) {
				out_error = "You must provide either a 'subagent_name', 'profile', or 'task' to invoke a subagent.";
				return false;
			}

			args_.name = raw_args.name;
			args_.subagent_name = raw_args.subagent_name;
			args_.profile = raw_args.profile;
			args_.task = raw_args.task;
			args_.wait = raw_args.wait;
			args_.local_only = raw_args.local_only;
			return true;
		} catch (const std::exception &e) {
			out_error = "Argument parsing error: " + std::string(e.what());
			return false;
		}
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json & /*args*/) const override
	{
		return std::make_unique<invoke_subagent_tool>(args_);
	}

      private:
	mutable invoke_subagent_args args_;
};

REGISTER_TOOL(invoke_subagent_validator)

} // namespace tools