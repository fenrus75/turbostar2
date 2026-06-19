#pragma once
#include <memory>
#include <string>
#include <vector>
#include "ai_model.h"
#include "llm_transport.h"
#include "llm_types.h"
#include "tool_registry.h"
#include "agent_role.h"

namespace agentlib
{

class Connection;

class llm_client
{
      public:
	explicit llm_client(std::shared_ptr<llm_transport> transport, std::string model_id, api_type type = api_type::openai);
	~llm_client();

	llm_chat_response send_chat(const std::vector<message> &conversation, const tool_registry *registry = nullptr,
				    const std::vector<std::string> &active_families = {}, const std::string &previous_response_id = "",
				    agent_role role = agent_role::developer);

	void send_chat_stream(const std::vector<message> &conversation, std::function<void(const chat_delta &)> callback,
			      const tool_registry *registry = nullptr, const std::vector<std::string> &active_families = {},
			      const std::string &previous_response_id = "", agent_role role = agent_role::developer);

	void cancel();
	std::string compact_response(const std::string &previous_response_id, std::string *error_msg = nullptr);

      private:
	std::shared_ptr<llm_transport> transport_;
	std::string model_id_;
	api_type type_;
	std::unique_ptr<Connection> connection_;
};

} // namespace agentlib
