#pragma once

#include "connection.h"
#include "../llm_transport.h"
#include "../ai_model.h"
#include <memory>
#include <string>

namespace agentlib {

class connection_factory {
public:
	static std::unique_ptr<Connection> create(
		std::shared_ptr<llm_transport> transport,
		const std::string& model_id,
		api_type type
	);
};

} // namespace agentlib
