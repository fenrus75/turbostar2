#pragma once

#include "connection.h"
#include "../llm_transport.h"
#include "../ai_model.h"
#include <memory>

namespace agentlib {

class openai_response_connection : public Connection {
public:
	openai_response_connection(std::shared_ptr<llm_transport> transport, std::string model_id, api_type type);
	~openai_response_connection() override = default;

	void initialize() override;
	void close() override;
	void sync_history(Conversation& convo) override;
	void send_prompt(
		Conversation& convo,
		const std::string& agent_identity,
		const std::vector<std::string>& active_families,
		std::function<void(const stream_event&)> callback
	) override;

	bool supports_compaction() const override { return true; }
	std::string compact_response(const std::string& previous_response_id, std::string* error_msg) override;

private:
	std::shared_ptr<llm_transport> transport_;
	std::string model_id_;
	api_type type_;
};

} // namespace agentlib
