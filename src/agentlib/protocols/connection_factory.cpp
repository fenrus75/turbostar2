#include "connection_factory.h"
#include "openai_completion_connection.h"
#include "openai_response_connection.h"
#include "gemini_connection.h"
#include "claude_connection.h"

namespace agentlib {

std::unique_ptr<Connection> connection_factory::create(
	std::shared_ptr<llm_transport> transport,
	const std::string& model_id,
	api_type type
) {
	if (type == api_type::gemini) {
		return std::make_unique<gemini_connection>(std::move(transport), model_id, type);
	} else if (type == api_type::openai_response) {
		return std::make_unique<openai_response_connection>(std::move(transport), model_id, type);
	} else if (type == api_type::claude) {
		return std::make_unique<claude_connection>(std::move(transport), model_id, type);
	} else {
		return std::make_unique<openai_completion_connection>(std::move(transport), model_id, type);
	}
}

} // namespace agentlib
