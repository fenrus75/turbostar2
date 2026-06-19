#pragma once

#include "connection.h"
#include "../llm_transport.h"
#include "../ai_model.h"
#include <memory>

namespace agentlib {

class claude_connection : public Connection {
public:
	claude_connection(std::shared_ptr<llm_transport> transport, std::string model_id, api_type type);
	~claude_connection() override = default;

	void initialize() override;
	void close() override;
	void sync_history(Conversation& convo) override;
	void send_prompt(
		Conversation& convo,
		const agent_properties& properties,
		const std::vector<std::string>& active_families,
		std::function<void(const stream_event&)> callback
	) override;

private:
	std::shared_ptr<llm_transport> transport_;
	std::string model_id_;
	api_type type_;
};

} // namespace agentlib
