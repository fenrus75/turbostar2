#include <cassert>
#include <iostream>
#include "agentlib/model_server.h"
#include "agentlib/ai_model.h"
#include "event_logger.h"
#include "fs_utils.h"

using namespace agentlib;

int main()
{
	std::cout << "Running test_model_server..." << std::endl;

	auto &registry = model_server_registry::get_instance();

	// Clear out any initial servers
	auto all_servers = registry.get_all_servers();
	for (const auto &s : all_servers) {
		registry.remove_server(s->get_id());
	}
	assert(registry.get_all_servers().empty());

	// Register a server
	auto s1 = std::make_shared<model_server>("openai_test", "OpenAI Test Server", "https://api.openai.com/v1", "key123", api_type::openai);
	registry.register_server(s1);

	// Verify lookup
	auto lookup1 = registry.get_server("openai_test");
	assert(lookup1 != nullptr);
	assert(lookup1->get_name() == "OpenAI Test Server");
	assert(lookup1->get_url() == "https://api.openai.com/v1");
	assert(lookup1->get_api_key() == "key123");
	assert(lookup1->get_api_type() == api_type::openai);

	// Verify update
	lookup1->set_name("Updated Name");
	registry.update_server(lookup1);
	auto lookup2 = registry.get_server("openai_test");
	assert(lookup2->get_name() == "Updated Name");

	// Verify saving and loading
	registry.save_servers();

	// Remove it
	registry.remove_server("openai_test");
	assert(registry.get_server("openai_test") == nullptr);

	// Reload from saved file
	registry.load_servers();
	auto reloaded = registry.get_server("openai_test");
	assert(reloaded != nullptr);
	assert(reloaded->get_name() == "Updated Name");
	assert(reloaded->get_url() == "https://api.openai.com/v1");

	// Verify ai_model server_id mapping
	auto &model_reg = ai_model_registry::get_instance();
	auto all_models = model_reg.get_all_models();
	for (const auto &m : all_models) {
		model_reg.remove_model(m->get_id());
	}

	auto test_model = std::make_shared<ai_model>("test_model_id", "Test Model", "url", "purpose", 0.0, 0.0, "", api_type::openai, 250000, model_cost_type::paid_per_token, "openai_test");
	assert(test_model->get_server_id() == "openai_test");
	assert(test_model->get_url() == "https://api.openai.com/v1");
	assert(test_model->get_api_key() == "key123");
	assert(test_model->get_api_type() == api_type::openai);
	assert(test_model->get_from_download() == false);

	auto test_downloaded_model = std::make_shared<ai_model>("test_downloaded_id", "Downloaded Model", "url", "purpose", 0.0, 0.0, "", api_type::openai, 250000, model_cost_type::paid_per_token, "openai_test", true);
	assert(test_downloaded_model->get_from_download() == true);

	// Test fallback server auto-generation
	auto fallback_model = std::make_shared<ai_model>("test_fallback_model", "Fallback Model", "https://fallback.api/v1", "purpose", 0.0, 0.0, "keyfallback", api_type::gemini);
	assert(!fallback_model->get_server_id().empty());
	auto fallback_server = registry.get_server(fallback_model->get_server_id());
	assert(fallback_server != nullptr);
	assert(fallback_server->get_url() == "https://fallback.api/v1");
	assert(fallback_server->get_api_key() == "keyfallback");
	assert(fallback_server->get_api_type() == api_type::gemini);
	assert(fallback_model->get_url() == "https://fallback.api/v1");
	assert(fallback_model->get_from_download() == false);

	model_reg.register_model(test_model);
	model_reg.register_model(test_downloaded_model);
	model_reg.register_model(fallback_model);
	model_reg.save_models();

	model_reg.remove_model("test_model_id");
	model_reg.remove_model("test_downloaded_id");
	model_reg.remove_model("test_fallback_model");
	assert(model_reg.get_model("test_model_id") == nullptr);
	assert(model_reg.get_model("test_downloaded_id") == nullptr);

	model_reg.load_models();
	auto reloaded_model = model_reg.get_model("test_model_id");
	assert(reloaded_model != nullptr);
	assert(reloaded_model->get_server_id() == "openai_test");
	assert(reloaded_model->get_url() == "https://api.openai.com/v1");
	assert(reloaded_model->get_from_download() == false);

	auto reloaded_downloaded = model_reg.get_model("test_downloaded_id");
	assert(reloaded_downloaded != nullptr);
	assert(reloaded_downloaded->get_from_download() == true);

	// Clean up model
	model_reg.remove_model("test_model_id");
	model_reg.remove_model("test_downloaded_id");
	model_reg.remove_model("test_fallback_model");
	model_reg.save_models();

	if (fallback_server) {
		registry.remove_server(fallback_server->get_id());
	}

	// Clean up
	registry.remove_server("openai_test");
	registry.save_servers();

	std::cout << "test_model_server passed!" << std::endl;
	return 0;
}
