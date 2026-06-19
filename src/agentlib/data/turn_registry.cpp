#include "turn_registry.h"
#include "system_turn.h"
#include "user_turn.h"
#include "model_response_turn.h"
#include "tool_execution_turn.h"
#include "error_turn.h"

namespace agentlib {

TurnRegistry::TurnRegistry() {
	register_deserializer("system", [](const nlohmann::json& j) { return system_turn::deserialize(j); });
	register_deserializer("user", [](const nlohmann::json& j) { return user_turn::deserialize(j); });
	register_deserializer("model_response", [](const nlohmann::json& j) { return model_response_turn::deserialize(j); });
	register_deserializer("tool_execution", [](const nlohmann::json& j) { return tool_execution_turn::deserialize(j); });
	register_deserializer("error", [](const nlohmann::json& j) { return error_turn::deserialize(j); });
}

TurnRegistry& TurnRegistry::get_instance() {
	static TurnRegistry instance;
	return instance;
}

void TurnRegistry::register_deserializer(const std::string& turn_type_name, 
										 std::function<std::shared_ptr<Turn>(const nlohmann::json&)> deserializer) {
	deserializers_[turn_type_name] = std::move(deserializer);
}

std::shared_ptr<Turn> TurnRegistry::deserialize(const nlohmann::json& j) const {
	std::string type = j.value("turn_type", "generic");
	if (deserializers_.contains(type)) {
		return deserializers_.at(type)(j);
	}
	
	// Fallback for tool specific turns (e.g. if plugin uninstalled)
	// If it contains "results" it's a tool execution turn.
	if (j.contains("results")) {
		return tool_execution_turn::deserialize(j);
	}
	
	// Fallback to error turn if unknown
	return error_turn::deserialize(j);
}

} // namespace agentlib
