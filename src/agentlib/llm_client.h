#pragma once
#include <memory>
#include <string>
#include <vector>
#include "ai_model.h"
#include "api_formatter.h"
#include "llm_transport.h"
#include "llm_types.h"
#include "tool_registry.h"

namespace agentlib
{

class llm_client
{
      public:
	explicit llm_client(std::shared_ptr<llm_transport> transport, std::string model_id, api_type type = api_type::openai);

	llm_chat_response send_chat(const std::vector<message> &conversation, const tool_registry *registry = nullptr,
				    const std::vector<std::string> &active_families = {}, const std::string &previous_response_id = "");

	void send_chat_stream(const std::vector<message> &conversation, std::function<void(const chat_delta &)> callback,
			      const tool_registry *registry = nullptr, const std::vector<std::string> &active_families = {},
			      const std::string &previous_response_id = "");

	void cancel();
	std::string compact_response(const std::string &previous_response_id);

      private:
	std::shared_ptr<llm_transport> transport_;
	std::string model_id_;
	std::unique_ptr<api_formatter> formatter_;
};

} // namespace agentlib
