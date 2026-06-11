#include <cassert>
#include <iostream>
#include <nlohmann/json.hpp>
#include "../../src/agentlib/api_formatter.h"

using namespace agentlib;
using json = nlohmann::json;

int main()
{
	std::cout << "Running test_api_formatter..." << std::endl;

	auto formatter = api_formatter::create(api_type::gemini);
	assert(formatter != nullptr);

	std::vector<message> convo;
	message msg1;
	msg1.role = "system";
	msg1.content = "First system instruction.";
	convo.push_back(msg1);

	message msg2;
	msg2.role = "system";
	msg2.content = "Second system instruction.";
	convo.push_back(msg2);

	message msg3;
	msg3.role = "user";
	msg3.content = "Hello";
	convo.push_back(msg3);

	std::string payload_str = formatter->build_chat_payload("gemini-1.5-pro", convo, nullptr, false);
	std::cout << "Generated payload: " << payload_str << std::endl;

	json payload = json::parse(payload_str);
	assert(payload.contains("systemInstruction"));
	assert(payload["systemInstruction"].contains("parts"));
	assert(!payload["systemInstruction"]["parts"].empty());

	std::string combined_text = payload["systemInstruction"]["parts"][0]["text"].get<std::string>();
	std::cout << "System Instruction text: " << combined_text << std::endl;

	// Assert both system messages are present in the Gemini payload
	assert(combined_text.find("First system instruction.") != std::string::npos);
	assert(combined_text.find("Second system instruction.") != std::string::npos);

	// Test OpenAI formatter endpoint path
	auto open_fmt = api_formatter::create(api_type::openai);
	assert(open_fmt != nullptr);
	assert(open_fmt->get_endpoint_path("gpt-4", false) == "/v1/chat/completions");

	// Test Copilot formatter endpoint path
	auto cop_fmt = api_formatter::create(api_type::copilot);
	assert(cop_fmt != nullptr);
	assert(cop_fmt->get_endpoint_path("gpt-4", false) == "/chat/completions");

	std::cout << "test_api_formatter passed successfully!" << std::endl;
	return 0;
}
