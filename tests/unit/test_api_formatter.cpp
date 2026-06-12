#include <cassert>
#include <iostream>
#include <nlohmann/json.hpp>
#include "../../src/agentlib/api_formatter.h"

using namespace agentlib;
using json = nlohmann::json;

class mock_compress_validator : public tool_validator
{
      public:
	std::string get_name() const override
	{
		return "agent_compress_history";
	}
	std::string get_description() const override
	{
		return "mock";
	}
	std::string get_family() const override
	{
		return "base";
	}
	nlohmann::json get_parameters_schema() const override
	{
		return {};
	}
	bool is_pure() const override
	{
		return false;
	}
	std::unique_ptr<llm_tool> create_tool_impl(const nlohmann::json &) const override
	{
		return nullptr;
	}
	bool validate_args_impl(const nlohmann::json &, const tool_context &, std::string &) const override
	{
		return true;
	}
};

class mock_normal_validator : public tool_validator
{
      public:
	std::string get_name() const override
	{
		return "mock_normal_tool";
	}
	std::string get_description() const override
	{
		return "mock";
	}
	std::string get_family() const override
	{
		return "base";
	}
	nlohmann::json get_parameters_schema() const override
	{
		return {};
	}
	bool is_pure() const override
	{
		return false;
	}
	std::unique_ptr<llm_tool> create_tool_impl(const nlohmann::json &) const override
	{
		return nullptr;
	}
	bool validate_args_impl(const nlohmann::json &, const tool_context &, std::string &) const override
	{
		return true;
	}
};

int main()
{
	std::cout << "Running test_api_formatter..." << std::endl;

	// Register mocks
	auto &registry = tool_registry::get_instance();
	registry.register_validator([]() { return std::make_unique<mock_compress_validator>(); });
	registry.register_validator([]() { return std::make_unique<mock_normal_validator>(); });

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

	// Test openai_response formatter
	auto resp_fmt = api_formatter::create(api_type::openai_response);
	assert(resp_fmt != nullptr);
	assert(resp_fmt->get_endpoint_path("gpt-4", false) == "/v1/chat/completions");

	// Test Copilot formatter endpoint path
	auto cop_fmt = api_formatter::create(api_type::copilot);
	assert(cop_fmt != nullptr);
	assert(cop_fmt->get_endpoint_path("gpt-4", false) == "/chat/completions");

	// Test tools filtering on openai_response
	json normal_tools = registry.get_tools_json({}, true);
	json resp_tools = registry.get_tools_json({}, false);

	bool has_compress_normal = false;
	for (const auto &t : normal_tools) {
		if (t["function"]["name"] == "agent_compress_history") {
			has_compress_normal = true;
		}
	}
	assert(has_compress_normal);

	bool has_compress_resp = false;
	for (const auto &t : resp_tools) {
		if (t["function"]["name"] == "agent_compress_history" || t["function"]["name"] == "agent_restore_context") {
			has_compress_resp = true;
		}
	}
	assert(!has_compress_resp);

	std::cout << "test_api_formatter passed successfully!" << std::endl;
	return 0;
}
