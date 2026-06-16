#include <cassert>
#include <iostream>
#include "agentlib/model_server.h"
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

	// Clean up
	registry.remove_server("openai_test");
	registry.save_servers();

	std::cout << "test_model_server passed!" << std::endl;
	return 0;
}
