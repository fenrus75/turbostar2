#include "tool_registry.h"
#include <iostream>
#include "ai_agent.h"

namespace agentlib
{

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

nlohmann::json tool_registry::get_tools_json(const std::vector<std::string> &active_families, bool mutation_possible) const
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	nlohmann::json tools_array = nlohmann::json::array();
	for (const auto &[name, factory] : validator_factories_) {
		auto validator = factory();
		if (!validator) {
			continue;
		}

		std::string tool_name = validator->get_name();
		if (!mutation_possible && (tool_name == "agent_compress_history" || tool_name == "agent_restore_context")) {
			continue;
		}

		std::string family = validator->get_family();
		bool allowed = true;
		if (!active_families.empty()) {
			allowed = (std::find(active_families.begin(), active_families.end(), family) != active_families.end());
		}

		if (!allowed) {
			continue;
		}

		std::string desc = validator->get_description();
		if (validator->is_pure()) {
			desc += " [Read-Only: Safe for Plan Mode]";
		} else if (validator->get_name() == "exit_plan_mode") {
			desc += " [State-Modifying: Allowed in Plan Mode]";
		} else {
			desc += " [State-Modifying: Blocked in Plan Mode]";
		}

		tool_name = validator->get_name();
		if (tool_name.starts_with("mcp:")) {
			tool_name = serialize_mcp_name(tool_name);
		}

		nlohmann::json tool_schema = {
		    {"type", "function"},
		    {"function", {{"name", tool_name}, {"description", desc}, {"parameters", validator->get_parameters_schema()}}}};
		tools_array.push_back(tool_schema);
	}
	return tools_array;
}

nlohmann::json tool_registry::get_gemini_tools_json(const std::vector<std::string> &active_families, bool mutation_possible) const
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	nlohmann::json tools_array = nlohmann::json::array();
	for (const auto &[name, factory] : validator_factories_) {
		auto validator = factory();
		if (!validator) {
			continue;
		}

		std::string tool_name = validator->get_name();
		if (!mutation_possible && (tool_name == "agent_compress_history" || tool_name == "agent_restore_context")) {
			continue;
		}

		std::string family = validator->get_family();
		bool allowed = true;
		if (!active_families.empty()) {
			allowed = (std::find(active_families.begin(), active_families.end(), family) != active_families.end());
		}

		if (!allowed) {
			continue;
		}

		std::string desc = validator->get_description();
		if (validator->is_pure()) {
			desc += " [Read-Only: Safe for Plan Mode]";
		} else if (validator->get_name() == "exit_plan_mode") {
			desc += " [State-Modifying: Allowed in Plan Mode]";
		} else {
			desc += " [State-Modifying: Blocked in Plan Mode]";
		}

		tool_name = validator->get_name();
		if (tool_name.starts_with("mcp:")) {
			tool_name = serialize_mcp_name(tool_name);
		}

		nlohmann::json func_decl = {{"name", tool_name}, {"description", desc}, {"parameters", validator->get_parameters_schema()}};
		tools_array.push_back(func_decl);
	}
	return nlohmann::json::array({{{"functionDeclarations", tools_array}}});
}

std::vector<std::string> tool_registry::get_all_registered_families() const
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	std::vector<std::string> families;
	for (const auto &[name, factory] : validator_factories_) {
		auto validator = factory();
		if (validator) {
			std::string family = validator->get_family();
			if (std::find(families.begin(), families.end(), family) == families.end()) {
				families.push_back(family);
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

	std::string family = validator->get_family();
	if (ctx.is_family_active && !ctx.is_family_active(family)) {
		res.error_message = "Security Violation: Tool family '" + family +
				    "' is not active. "
				    "You must call activate_tool_family(\"" +
				    family + "\") first to use this tool.";
		return res;
	}

	if (ctx.active_agent && ctx.active_agent->is_read_only() && !validator->is_pure()) {
		res.error_message =
		    "Security Violation: Agent is in read-only mode and cannot execute state-modifying tool '" + name + "'.";
		return res;
	}

	nlohmann::json args;
	try {
		args = nlohmann::json::parse(args_json_string);
	} catch (const std::exception &e) {
		res.error_message = "Error parsing tool arguments: " + std::string(e.what());
		return res;
	}

	if (ctx.active_agent && ctx.active_agent->is_planning() && name != "exit_plan_mode") {
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

	// Execution
	try {
		return prep.tool->execute(ctx);
	} catch (const std::exception &e) {
		return "Execution Error: " + std::string(e.what());
	}
}

} // namespace agentlib
