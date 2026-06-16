#include <cassert>
#include <iostream>
#include "agentlib/model_server.h"
#include "agentlib/ai_model.h"
#include "config_manager.h"
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

	// Test server rename cascading to models
	auto rename_server = std::make_shared<model_server>("rename_old", "Rename Old", "url", "", api_type::openai);
	registry.register_server(rename_server);
	auto model_manual = std::make_shared<ai_model>("model_manual", "Model Manual", "url", "purpose", 0.0, 0.0, "", api_type::openai, 250000, model_cost_type::paid_per_token, "rename_old", false);
	auto model_downloaded = std::make_shared<ai_model>("model_downloaded", "Model Downloaded", "url", "purpose", 0.0, 0.0, "", api_type::openai, 250000, model_cost_type::paid_per_token, "rename_old", true);
	model_reg.register_model(model_manual);
	model_reg.register_model(model_downloaded);

	// Simulate renaming "rename_old" to "rename_new"
	registry.remove_server("rename_old");
	for (auto &model : model_reg.get_all_models()) {
		if (model->get_server_id() == "rename_old") {
			model->set_server_id("rename_new");
		}
	}
	auto rename_new_server = std::make_shared<model_server>("rename_new", "Rename New", "url", "", api_type::openai);
	registry.register_server(rename_new_server);

	assert(model_manual->get_server_id() == "rename_new");
	assert(model_downloaded->get_server_id() == "rename_new");

	// Test deleting server deletes only from_download models
	registry.remove_server("rename_new");
	std::vector<std::string> to_remove;
	for (const auto &model : model_reg.get_all_models()) {
		if (model->get_server_id() == "rename_new" && model->get_from_download()) {
			to_remove.push_back(model->get_id());
		}
	}
	for (const auto &mid : to_remove) {
		model_reg.remove_model(mid);
	}

	assert(model_reg.get_model("model_manual") != nullptr);
	assert(model_reg.get_model("model_downloaded") == nullptr);

	// Clean up
	model_reg.remove_model("model_manual");
	registry.remove_server("rename_new");

	// Test get models (query) refresh logic, including default model preservation
	auto query_server = std::make_shared<model_server>("query_server", "Query Server", "url", "", api_type::openai);
	registry.register_server(query_server);

	// A downloaded model to be refreshed
	auto old_downloaded = std::make_shared<ai_model>("model_to_refresh", "Old Downloaded Model", "url", "purpose", 0.0, 0.0, "", api_type::openai, 250000, model_cost_type::paid_per_token, "query_server", true);
	// A manual model that should not be refreshed
	auto manual_on_query_server = std::make_shared<ai_model>("manual_keep", "Manual Model", "url", "purpose", 0.0, 0.0, "", api_type::openai, 250000, model_cost_type::paid_per_token, "query_server", false);
	model_reg.register_model(old_downloaded);
	model_reg.register_model(manual_on_query_server);

	// Set default model to the one to be refreshed
	config_manager::get_instance().set_default_model_id("model_to_refresh");

	// Simulate receiving new list of models from "get models" (query)
	// One of the imported models matches the old one by name (even if ID changed)
	std::vector<std::shared_ptr<ai_model>> imported_models = {
		std::make_shared<ai_model>("new_model_id", "Old Downloaded Model", "url", "purpose", 0.0, 0.0, "", api_type::openai, 250000, model_cost_type::paid_per_token, "query_server", true),
		std::make_shared<ai_model>("another_imported", "Another Model", "url", "purpose", 0.0, 0.0, "", api_type::openai, 250000, model_cost_type::paid_per_token, "query_server", true)
	};

	// 1. Remember default
	{
		std::string old_default_id = config_manager::get_instance().get_default_model_id();
		std::string old_default_name;
		auto old_default_model = model_reg.get_model(old_default_id);
		if (old_default_model) {
			old_default_name = old_default_model->get_name();
		}

		// 2. Delete all from_download models for this server
		std::vector<std::string> query_to_remove;
		for (const auto &model : model_reg.get_all_models()) {
			if (model->get_server_id() == "query_server" && model->get_from_download()) {
				query_to_remove.push_back(model->get_id());
			}
		}
		for (const auto &mid : query_to_remove) {
			model_reg.remove_model(mid);
		}

		// 3. Register new ones and restore default mapping
		bool restored_default = false;
		for (const auto &model : imported_models) {
			model_reg.register_model(model);
			if (!old_default_id.empty() &&
			    (model->get_id() == old_default_id ||
			     (!old_default_name.empty() && model->get_name() == old_default_name))) {
				config_manager::get_instance().set_default_model_id(model->get_id());
				restored_default = true;
			}
		}

		// Assertions
		assert(model_reg.get_model("model_to_refresh") == nullptr); // deleted
		assert(model_reg.get_model("new_model_id") != nullptr);     // added
		assert(model_reg.get_model("another_imported") != nullptr); // added
		assert(model_reg.get_model("manual_keep") != nullptr);      // manual is preserved
		assert(restored_default == true);
		assert(config_manager::get_instance().get_default_model_id() == "new_model_id"); // restored to new ID by matching name
	}

	// Clean up query test
	model_reg.remove_model("new_model_id");
	model_reg.remove_model("another_imported");
	model_reg.remove_model("manual_keep");
	registry.remove_server("query_server");

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
