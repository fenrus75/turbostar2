#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include "../../src/agentlib/ai_model.h"
#include "../../src/agentlib/model_server.h"
#include "../../src/agentlib/stale_models.h"
#include "test_watchdog.h"

using namespace agentlib;

int main()
{
	test_watchdog::setup_watchdog(30);
	std::cout << "Testing AI model dynamic scoring and sorting..." << std::endl;

	// 1. Test Stale Models Lookup
	assert(is_stale_model("gpt-3.5-turbo-0301") == true);
	assert(is_stale_model("claude-2.0") == true);
	assert(is_stale_model("gemini-2.5-flash") == false);

	// 2. Setup Test Servers
	auto vendor_server = std::make_shared<model_server>("vendor_srv", "Vendor Server", "https://api.openai.com", "", api_type::openai, -2.0);
	auto custom_server = std::make_shared<model_server>("custom_srv", "Custom Server", "http://localhost:11434", "", api_type::openai, 0.0);
	model_server_registry::get_instance().register_server(vendor_server);
	model_server_registry::get_instance().register_server(custom_server);

	// 3. Test Version Bonus & Server Base Score
	auto custom_m1 = std::make_shared<ai_model>("custom-llama-3.1", "Custom Llama 3.1", "http://localhost:11434", "", 0.0, 0.0, "", api_type::openai, 100000, model_cost_type::free_local, "custom_srv");
	auto vendor_m25 = std::make_shared<ai_model>("gemini-2.5-flash", "Gemini 2.5 Flash", "https://api.openai.com", "", 0.0, 0.0, "", api_type::openai, 100000, model_cost_type::paid_per_token, "vendor_srv");
	auto vendor_m20 = std::make_shared<ai_model>("gemini-2.0-flash", "Gemini 2.0 Flash", "https://api.openai.com", "", 0.0, 0.0, "", api_type::openai, 100000, model_cost_type::paid_per_token, "vendor_srv");
	auto vendor_m15 = std::make_shared<ai_model>("gemini-1.5-flash", "Gemini 1.5 Flash", "https://api.openai.com", "", 0.0, 0.0, "", api_type::openai, 100000, model_cost_type::paid_per_token, "vendor_srv");

	// custom_m1: base_score 0.0 + version 0.31 = 0.31
	// vendor_m25: base_score -2.0 + version 0.25 = -1.75
	// vendor_m20: base_score -2.0 + version 0.20 = -1.80
	// vendor_m15: base_score -2.0 + version 0.15 = -1.85
	assert(custom_m1->calculate_score() > vendor_m25->calculate_score());
	assert(vendor_m25->calculate_score() > vendor_m20->calculate_score());
	assert(vendor_m20->calculate_score() > vendor_m15->calculate_score());

	// 4. Test Age Decay Penalty
	auto now_sec = static_cast<uint64_t>(
	    std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
	auto fresh_model = std::make_shared<ai_model>("model-new", "Model New", "https://api.openai.com", "", 0.0, 0.0, "", api_type::openai, 100000, model_cost_type::paid_per_token, "vendor_srv");
	fresh_model->set_creation_timestamp(now_sec);

	auto old_model = std::make_shared<ai_model>("model-old", "Model Old", "https://api.openai.com", "", 0.0, 0.0, "", api_type::openai, 100000, model_cost_type::paid_per_token, "vendor_srv");
	old_model->set_creation_timestamp(now_sec - (365 * 86400)); // 1 year old

	assert(fresh_model->calculate_score() > old_model->calculate_score());

	// 5. Test Stale Model Penalty
	auto stale_model = std::make_shared<ai_model>("gpt-3.5-turbo-0301", "GPT 3.5 Turbo 0301", "https://api.openai.com", "", 0.0, 0.0, "", api_type::openai, 100000, model_cost_type::paid_per_token, "vendor_srv");
	auto active_model = std::make_shared<ai_model>("gpt-4o", "GPT 4o", "https://api.openai.com", "", 0.0, 0.0, "", api_type::openai, 100000, model_cost_type::paid_per_token, "vendor_srv");
	assert(active_model->calculate_score() > stale_model->calculate_score());

	// 6. Test Registry Sorting Order
	ai_model_registry::get_instance().register_model(vendor_m15);
	ai_model_registry::get_instance().register_model(vendor_m25);
	ai_model_registry::get_instance().register_model(vendor_m20);
	ai_model_registry::get_instance().register_model(custom_m1);

	auto sorted_models = ai_model_registry::get_instance().get_all_models();
	assert(sorted_models.size() >= 4);
	assert(sorted_models[0]->get_id() == "custom-llama-3.1");

	std::cout << "All AI model scoring and sorting tests passed!" << std::endl;
	return 0;
}
