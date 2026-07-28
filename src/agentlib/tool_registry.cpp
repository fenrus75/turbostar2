#include "tool_registry.h"
#include <iostream>
#include "ai_agent.h"
#include "statistics_manager.h"

namespace agentlib
{

namespace
{
static std::vector<std::string> split_families(const std::string &s)
{
	std::vector<std::string> res;
	size_t start = 0, end;
	while ((end = s.find('|', start)) != std::string::npos) {
		res.push_back(s.substr(start, end - start));
		start = end + 1;
	}
	res.push_back(s.substr(start));
	return res;
}
} // namespace

tool_registry::tool_registry()
{
	register_tool_family(
		"git",
		"Activate when performing git operations (add, commit, status, diff, log, branch, pull, push, restore)",
		"The 'git' tool family contains tools to interact with Git version control.\n\n"
		"Key Tools:\n"
		"- git_status: Inspect files changed in the workspace.\n"
		"- git_diff_unstaged / git_diff_staged: Inspect file diffs.\n"
		"- git_add: Stage changes.\n"
		"- git_commit: Commit staged changes.\n"
		"- git_blame: View commit-level line history."
	);
}

tool_registry &tool_registry::get_instance()
{
	static tool_registry instance;
	return instance;
}

void tool_registry::register_validator(validator_factory factory)
{
	// Instantiate a dummy to get its name for the registry map
	auto dummy = factory();
	if (dummy) {
		std::string name = dummy->get_name();
		std::lock_guard<std::recursive_mutex> lock(mutex_);
		validator_factories_[name] = std::move(factory);
	}
}

void tool_registry::unregister_validator(const std::string &name)
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	validator_factories_.erase(name);
}

void tool_registry::register_tool_family(const std::string &name, const std::string &reason, const std::string &guidance)
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	family_reasons_[name] = reason;
	if (!guidance.empty()) {
		family_guidances_[name] = guidance;
	}
}

void tool_registry::unregister_tool_family(const std::string &name)
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	family_reasons_.erase(name);
	family_guidances_.erase(name);
}

std::string tool_registry::get_tool_family_reason(const std::string &name) const
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	auto it = family_reasons_.find(name);
	if (it != family_reasons_.end()) {
		return it->second;
	}
	return "";
}

std::string tool_registry::get_tool_family_guidance(const std::string &name) const
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	auto it = family_guidances_.find(name);
	if (it != family_guidances_.end()) {
		return it->second;
	}
	return "";
}

bool tool_registry::has_tool_family(const std::string &name) const
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	return family_reasons_.contains(name);
}

tool_registry::~tool_registry()
{
}

static std::string serialize_mcp_name(const std::string &name)
{
	if (!name.starts_with("mcp:")) {
		return name;
	}
	std::string res = name;
	size_t pos = 0;
	while ((pos = res.find(':', pos)) != std::string::npos) {
		res.replace(pos, 1, "__");
		pos += 2;
	}
	return res;
}

nlohmann::json tool_registry::get_tools_json(bool mutation_possible, const agent_properties &properties) const
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	nlohmann::json tools_array = nlohmann::json::array();
	for (const auto &[name, factory] : validator_factories_) {
		auto validator = factory();
		if (!validator) {
			continue;
		}

		if (!validator->is_allowed_for_agent(properties)) {
			continue;
		}

		std::string tool_name = validator->get_name();
		if (!mutation_possible && (tool_name == "agent_compress_history" || tool_name == "agent_restore_context")) {
			continue;
		}

		std::string desc = validator->get_description();
		if (validator->is_allowed_in_plan_mode_statically()) {
			if (validator->is_pure()) {
				desc += " [Read-Only: Safe for Plan Mode]";
			} else {
				desc += " [State-Modifying: Allowed in Plan Mode]";
			}
		} else {
			desc += " [State-Modifying: Blocked in Plan Mode]";
		}

		tool_name = validator->get_name();
		if (tool_name.starts_with("mcp:")) {
			tool_name = serialize_mcp_name(tool_name);
		}

		nlohmann::json params = validator->get_parameters_schema();
		if (!mutation_possible) {
			if (params.contains("properties") && params["properties"].is_object()) {
				params["properties"].erase("async");
				params["properties"].erase("is_async");
			}
			if (params.contains("required") && params["required"].is_array()) {
				nlohmann::json new_req = nlohmann::json::array();
				for (const auto &item : params["required"]) {
					if (item.is_string() && (item.get<std::string>() == "async" || item.get<std::string>() == "is_async")) {
						continue;
					}
					new_req.push_back(item);
				}
				params["required"] = new_req;
			}
		}

		nlohmann::json tool_schema = {
		    {"type", "function"},
		    {"function", {{"name", tool_name}, {"description", desc}, {"parameters", params}}}};
		tools_array.push_back(tool_schema);
	}
	return tools_array;
}

std::vector<std::shared_ptr<tool_validator>> tool_registry::get_active_tools(
	bool mutation_possible,
	const agent_properties &properties
) const {
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	std::vector<std::shared_ptr<tool_validator>> active_validators;
	for (const auto &[name, factory] : validator_factories_) {
		auto validator = factory();
		if (!validator) {
			continue;
		}

		if (!validator->is_allowed_for_agent(properties)) {
			continue;
		}

		std::string tool_name = validator->get_name();
		if (!mutation_possible && (tool_name == "agent_compress_history" || tool_name == "agent_restore_context")) {
			continue;
		}

		active_validators.push_back(std::shared_ptr<tool_validator>(std::move(validator)));
	}
	return active_validators;
}

std::vector<std::string> tool_registry::get_all_registered_families() const
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	std::vector<std::string> families;
	for (const auto &[name, factory] : validator_factories_) {
		auto validator = factory();
		if (validator) {
			std::string family_str = validator->get_family();
			for (const auto &fam : split_families(family_str)) {
				if (std::find(families.begin(), families.end(), fam) == families.end()) {
					families.push_back(fam);
				}
			}
		}
	}
	return families;
}

bool tool_registry::is_tool_silent(const std::string &name) const
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	auto it = validator_factories_.find(name);
	if (it != validator_factories_.end()) {
		auto validator = it->second();
		return validator->is_silent_by_default();
	}
	return false;
}

tool_registry::tool_preparation_result tool_registry::prepare_tool(const std::string &name, const std::string &args_json_string,
								   tool_context &ctx) const
{
	tool_preparation_result res;
	validator_factory factory;
	{
		std::lock_guard<std::recursive_mutex> lock(mutex_);
		auto it = validator_factories_.find(name);
		if (it == validator_factories_.end()) {
			res.error_message = "Error: tool not found.";
			return res;
		}
		factory = it->second;
	}

	// Create a transient validator instance for this execution to ensure thread-safe state!
	auto validator = factory();

	std::string family_str = validator->get_family();
	bool any_active = false;
	for (const auto &fam : split_families(family_str)) {
		if (!ctx.is_family_active || ctx.is_family_active(fam)) {
			any_active = true;
			break;
		}
	}
	if (!any_active) {
		res.error_message = "Security Violation: Tool family '" + family_str +
				    "' is not active. "
				    "You must call activate_tool_family() first to use this tool.";
		return res;
	}

	if (!validator->is_allowed_for_agent(ctx.properties)) {
		res.error_message =
		    "Security Violation: Agent role does not permit executing tool '" + name + "'.";
		return res;
	}

	if (ctx.properties.read_only && !validator->is_pure()) {
		bool allowed_in_plan = (ctx.active_agent && ctx.active_agent->is_planning() && validator->is_allowed_in_plan_mode_statically());
		if (!allowed_in_plan) {
			res.error_message =
			    "Security Violation: Agent is in read-only mode and cannot execute state-modifying tool '" + name + "'.";
			return res;
		}
	}

	nlohmann::json args;
	try {
		args = nlohmann::json::parse(args_json_string);
	} catch (const std::exception &e) {
		res.error_message = "Error parsing tool arguments: " + std::string(e.what());
		return res;
	}

	if (ctx.active_agent && ctx.active_agent->is_planning()) {
		if (!validator->is_allowed_in_plan_mode(args, ctx)) {
			res.error_message =
			    "Security Violation: Agent is currently in Plan Mode and cannot execute state-modifying tool '" + name +
			    "'. You must call exit_plan_mode first, or only edit the designated plan file.";
			return res;
		}
	}

	// Stage 1 Security: Pre-invocation validation
	std::string security_error;

	try {
		if (!validator->validate_args(args, ctx, security_error)) {
			res.error_message = "Stage 1 Security Violation: " + security_error;
			return res;
		}

		// Create the tool instance (will fail if validate_args wasn't called)
		res.tool = validator->create_tool(args);
		if (!res.tool) {
			res.error_message = "Error: Failed to instantiate tool. Validation state invalid.";
			return res;
		}
	} catch (const std::exception &e) {
		res.error_message = "Error parsing tool arguments: " + std::string(e.what());
		return res;
	}

	// Stage 2 Security: Runtime/Contextual validation
	if (!res.tool->validate_runtime(ctx, security_error)) {
		res.error_message = "Stage 2 Security Violation: " + security_error;
		res.tool.reset();
		return res;
	}

	return res;
}

std::string tool_registry::execute_tool(const std::string &name, const std::string &args_json_string, tool_context &ctx) const
{
	auto prep = prepare_tool(name, args_json_string, ctx);
	if (!prep.error_message.empty()) {
		return prep.error_message;
	}

	// Increment persistent statistics for the tool execution count
	statistics_manager::get_instance().increment_stat(std::format("toolcall:{}", name));

	// Execution
	try {
		return prep.tool->execute(ctx);
	} catch (const std::exception &e) {
		return "Execution Error: " + std::string(e.what());
	}
}

} // namespace agentlib
