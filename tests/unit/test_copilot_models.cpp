#include <cassert>
#include <iostream>
#include <nlohmann/json.hpp>
#include "../../src/agentlib/copilot_manager.h"
#include "../../src/event_logger.h"

using namespace agentlib;
using json = nlohmann::json;

int main()
{
	std::cout << "Running test_copilot_models..." << std::endl;

	std::string mock_catalog = R"([
		{
			"id": "gpt-4o",
			"name": "GPT-4o Model",
			"registry": "azure",
			"publisher": "OpenAI",
			"summary": "State-of-the-art multimodal model.",
			"rate_limit_tier": "tier1",
			"html_url": "https://example.com/gpt-4o",
			"version": "1.0",
			"capabilities": ["chat", "vision"],
			"limits": {
				"max_input_tokens": 128000,
				"max_output_tokens": 4096
			},
			"tags": ["multimodal", "fast"],
			"supported_input_modalities": ["text", "image"],
			"supported_output_modalities": ["text"]
		},
		{
			"id": "claude-3-5-sonnet",
			"name": "Claude 3.5 Sonnet",
			"publisher": "Anthropic",
			"summary": "High performance text model.",
			"limits": {
				"max_input_tokens": 200000
			}
		}
	])";

	std::string formatted_str = copilot_manager::format_github_models_json(mock_catalog);
	std::cout << "Formatted JSON:" << std::endl << formatted_str << std::endl;

	json formatted = json::parse(formatted_str);
	assert(formatted.is_array());
	assert(formatted.size() == 2);

	// Test first model
	auto model1 = formatted[0];
	assert(model1["id"] == "gpt-4o");
	assert(model1["name"] == "GPT-4o Model");
	assert(model1["url"] == "https://models.inference.ai.azure.com");
	assert(model1["purpose"] == "Publisher: OpenAI. State-of-the-art multimodal model.");
	assert(model1["cost_tx"] == 0.0);
	assert(model1["cost_rx"] == 0.0);
	assert(model1["api_key"] == "");
	assert(model1["api_type"] == "copilot");
	assert(model1["max_context_tokens"] == 128000);
	assert(model1["cost_type"] == "paid_per_token");

	// Test second model
	auto model2 = formatted[1];
	assert(model2["id"] == "claude-3-5-sonnet");
	assert(model2["name"] == "Claude 3.5 Sonnet");
	assert(model2["url"] == "https://models.inference.ai.azure.com");
	assert(model2["purpose"] == "Publisher: Anthropic. High performance text model.");
	assert(model2["max_context_tokens"] == 200000);

	// Test the polling throttling logic and ensure no initial throttle due to min() time_point underflow
	std::cout << "Testing polling throttle initial state..." << std::endl;
	bool res_poll1 = copilot_manager::get_instance().poll_device_authorization(5);
	(void)res_poll1;

	auto match_initial = event_logger::get_instance().get_latest_matching_message("GitHub Access Token Poll throttled");
	assert(!match_initial.has_value() && "First poll should not be throttled due to underflow/overflow");

	// A second poll immediately after should be throttled
	bool res_poll2 = copilot_manager::get_instance().poll_device_authorization(5);
	(void)res_poll2;
	auto match_immediate = event_logger::get_instance().get_latest_matching_message("GitHub Access Token Poll throttled");
	assert(match_immediate.has_value() && "Immediate second poll must be throttled");

	std::cout << "test_copilot_models passed successfully!" << std::endl;
	return 0;
}
