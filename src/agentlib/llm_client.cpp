#include "llm_client.h"
#include "protocols/connection_factory.h"
#include "protocols/connection.h"
#include "data/conversation.h"
#include "data/system_turn.h"
#include "data/user_turn.h"
#include "data/model_response_turn.h"
#include "data/tool_execution_turn.h"
#include "data/transaction.h"
#include <iostream>
#include <map>

namespace agentlib
{

llm_client::llm_client(std::shared_ptr<llm_transport> transport, std::string model_id, api_type type)
    : transport_(std::move(transport)), model_id_(std::move(model_id)), type_(type)
{
}

llm_client::~llm_client() = default;

static std::shared_ptr<Conversation> build_temp_conversation(const std::vector<message>& conversation) {
	auto convo = std::make_shared<Conversation>();
	auto ep = convo->create_new_episode("temp_ep_id", "temp_ep_title", "temp_ep_summary");
	
	auto tx = std::make_shared<Transaction>("temp_tx_id", transaction_type::user_exchange);
	ep->add_transaction(tx);

	int id_counter = 1;
	for (const auto& msg : conversation) {
		std::string id = "turn_" + std::to_string(id_counter++);
		if (msg.role == "system") {
			tx->add_turn(std::make_shared<system_turn>(id, msg.content, "base"));
		} else if (msg.role == "user") {
			tx->add_turn(std::make_shared<user_turn>(id, msg.content, msg.name));
		} else if (msg.role == "assistant") {
			tx->add_turn(std::make_shared<model_response_turn>(
				id, msg.content, msg.reasoning_content,
				msg.tool_calls ? *msg.tool_calls : std::vector<tool_call>{},
				""
			));
		} else if (msg.role == "tool") {
			tool_result tr;
			tr.call_id = msg.tool_call_id ? *msg.tool_call_id : "";
			tr.name = msg.name ? *msg.name : "";
			tr.content = msg.content;
			tr.is_error = false;
			tx->add_turn(std::make_shared<tool_execution_turn>(id, std::vector<tool_result>{tr}));
		}
	}
	return convo;
}

llm_chat_response llm_client::send_chat(const std::vector<message> &conversation, const tool_registry * /*registry*/,
					const std::vector<std::string> &active_families, const std::string &previous_response_id,
					const agent_properties &properties)
{
	llm_chat_response chat_response;
	chat_response.msg.role = "assistant";
	
	send_chat_stream(conversation, [&](const chat_delta &delta) {
		if (!delta.content.empty()) {
			chat_response.msg.content += delta.content;
		}
		if (!delta.reasoning_content.empty()) {
			if (!chat_response.msg.reasoning_content) {
				chat_response.msg.reasoning_content = "";
			}
			*chat_response.msg.reasoning_content += delta.reasoning_content;
		}
		if (delta.tool_calls) {
			if (!chat_response.msg.tool_calls) {
				chat_response.msg.tool_calls = std::vector<tool_call>();
			}
			for (const auto& tc : *delta.tool_calls) {
				bool merged = false;
				for (auto& existing : *chat_response.msg.tool_calls) {
					if (!tc.id.empty() && existing.id == tc.id) {
						existing.function.arguments += tc.function.arguments;
						merged = true;
						break;
					}
					if (!tc.function.name.empty() && existing.function.name == tc.function.name && tc.id.empty()) {
						existing.function.arguments += tc.function.arguments;
						merged = true;
						break;
					}
				}
				if (!merged) {
					chat_response.msg.tool_calls->push_back(tc);
				}
			}
		}
		if (delta.usage.total_tokens > 0) {
			chat_response.usage = delta.usage;
		}
		if (!delta.response_id.empty()) {
			chat_response.response_id = delta.response_id;
		}
	}, nullptr, active_families, previous_response_id, properties);

	return chat_response;
}

void llm_client::send_chat_stream(const std::vector<message> &conversation, std::function<void(const chat_delta &)> callback,
				  const tool_registry * /*registry*/, const std::vector<std::string> &active_families,
				  const std::string &previous_response_id, const agent_properties &properties)
{
	if (!connection_) {
		connection_ = connection_factory::create(transport_, model_id_, type_);
	}

	auto convo = build_temp_conversation(conversation);
	if (!previous_response_id.empty()) {
		nlohmann::json metadata = {{"last_response_id", previous_response_id}};
		convo->set_connection_state({{"metadata", metadata}});
	}

	connection_->send_prompt(*convo, properties, active_families, [callback](const stream_event& ev) {
		chat_delta delta;
		delta.response_id = ev.response_id;
		delta.usage = ev.usage;

		if (ev.type == stream_event::event_type::content_chunk) {
			delta.content = ev.text;
			callback(delta);
		} else if (ev.type == stream_event::event_type::reasoning_chunk) {
			delta.reasoning_content = ev.text;
			callback(delta);
		} else if (ev.type == stream_event::event_type::tool_call_delta) {
			delta.tool_calls = ev.tool_calls;
			callback(delta);
		} else if (ev.type == stream_event::event_type::completed) {
			delta.is_final = true;
			callback(delta);
		} else if (ev.type == stream_event::event_type::error) {
			delta.content = ev.text;
			delta.is_final = true;
			callback(delta);
		}
	});
}

void llm_client::cancel()
{
	if (connection_) {
		connection_->close();
	}
}

std::string llm_client::compact_response(const std::string &previous_response_id, std::string *error_msg)
{
	if (!connection_) {
		connection_ = connection_factory::create(transport_, model_id_, type_);
	}
	return connection_->compact_response(previous_response_id, error_msg);
}

} // namespace agentlib
