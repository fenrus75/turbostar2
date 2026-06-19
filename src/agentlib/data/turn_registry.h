#pragma once

#include <memory>
#include <string>
#include <map>
#include <functional>
#include <nlohmann/json.hpp>
#include "turn.h"

namespace agentlib {

class TurnRegistry {
public:
	static TurnRegistry& get_instance();

	void register_deserializer(const std::string& turn_type_name, 
							   std::function<std::shared_ptr<Turn>(const nlohmann::json&)> deserializer);

	std::shared_ptr<Turn> deserialize(const nlohmann::json& j) const;

private:
	TurnRegistry();
	std::map<std::string, std::function<std::shared_ptr<Turn>(const nlohmann::json&)>> deserializers_;
};

} // namespace agentlib
