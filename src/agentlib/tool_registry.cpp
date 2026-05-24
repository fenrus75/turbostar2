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
		validator_factories_[name] = std::move(factory);
	}
}

nlohmann::json tool_registry::get_tools_json() const
{
	nlohmann::json tools_array = nlohmann::json::array();
	for (const auto &[name, factory] : validator_factories_) {
		auto validator = factory();
		nlohmann::json tool_schema = {{"type", "function"},
					      {"function",
					       {{"name", validator->get_name()},
						{"description", validator->get_description()},
						{"parameters", validator->get_parameters_schema()}}}};
		tools_array.push_back(tool_schema);
	}
	return tools_array;
}

bool tool_registry::is_tool_silent(const std::string &name) const
{
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
	auto it = validator_factories_.find(name);
	if (it == validator_factories_.end()) {
		res.error_message = "Error: tool not found.";
		return res;
	}

	// Create a transient validator instance for this execution to ensure thread-safe state!
	auto validator = it->second();

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
