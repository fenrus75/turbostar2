#pragma once

#include "connection.h"
#include "../llm_transport.h"
#include "../ai_model.h"
#include <memory>

namespace agentlib {

class openai_completion_connection : public Connection {
public:
	openai_completion_connection(std::shared_ptr<llm_transport> transport, std::string model_id, api_type type);
	~openai_completion_connection() override = default;

	void initialize() override;
	void close() override;
	void sync_history(Conversation& convo) override;
	void send_prompt(
		Conversation& convo,
		const agent_properties& properties,
		std::function<void(const stream_event&)> callback
	) override;

private:
	std::shared_ptr<llm_transport> transport_;
	std::string model_id_;
	api_type type_;
};

} // namespace agentlib
