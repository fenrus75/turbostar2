#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include <nlohmann/json.hpp>
#include <httplib.h>
#include "../../src/a2a/a2a_server.h"
#include "../../src/agentlib/command_registry.h"
#include "../../src/agentlib/skill_manager.h"
#include "../../src/agentlib/subagent_manager.h"
#include "../../src/pluginloader.h"
#include "../../src/project_manager.h"

using namespace a2a;

int main()
{
	test_watchdog::setup_watchdog(30);
	project_manager::get_instance().initialize();

	(void)command_registry::get_instance();
	(void)agentlib::skill_manager::get_instance();

	plugin_loader::get_instance().load_all_plugins();
	agentlib::subagent_manager::get_instance().initialize();

	std::cout << "Testing a2a_server..." << std::endl;

	a2a_server &server = a2a_server::get_instance();
	int bound_port = 0;
	bool started = server.start(7820, &bound_port);
	std::cout << "Server started on port: " << bound_port << " status: " << started << std::endl;
	assert(started);
	assert(bound_port >= 7820);
	server.set_default_model("gemini-2.5-pro");
	assert(server.get_default_model() == "gemini-2.5-pro");

	httplib::Client client(std::format("http://127.0.0.1:{}", bound_port));

	// 1. Test GET /.well-known/agent.json
	{
		auto res = client.Get("/.well-known/agent.json");
		assert(res != nullptr);
		assert(res->status == 200);
		auto json_body = nlohmann::json::parse(res->body);
		assert(json_body["protocol_version"] == "1.0");
		assert(json_body["hosted_agents"].is_array());
		std::cout << "Root agent.json OK\n";
	}

	// 2. Test GET /a2a/v1/cards
	{
		auto res = client.Get("/a2a/v1/cards");
		assert(res != nullptr);
		assert(res->status == 200);
		auto json_body = nlohmann::json::parse(res->body);
		assert(json_body.is_array());
		std::cout << "Cards index OK (" << json_body.size() << " cards)\n";
	}

	// 3. Test GET /a2a/v1/cards/research
	{
		auto res = client.Get("/a2a/v1/cards/research");
		assert(res != nullptr);
		assert(res->status == 200);
		auto json_body = nlohmann::json::parse(res->body);
		assert(json_body["name"] == "research");
		std::cout << "Research card detail OK\n";
	}

	// Test disabling A2A exposure
	agentlib::subagent_manager::get_instance().set_subagent_a2a_exposed("research", false);
	assert(!agentlib::subagent_manager::get_instance().is_subagent_a2a_exposed("research"));
	{
		httplib::Client unexp_client(std::format("http://127.0.0.1:{}", bound_port));
		auto res = unexp_client.Get("/a2a/v1/cards/research");
		assert(res != nullptr);
		assert(res->status == 404);
		std::cout << "Unexposed agent card 404 OK\n";
	}
	agentlib::subagent_manager::get_instance().set_subagent_a2a_exposed("research", true);
	assert(agentlib::subagent_manager::get_instance().is_subagent_a2a_exposed("research"));

	// 4. Test POST /a2a/v1/agents/research/tasks
	std::string task_id;
	{
		nlohmann::json task_req = {{"instructions", "Audit code"}};
		auto res = client.Post("/a2a/v1/agents/research/tasks", task_req.dump(), "application/json");
		assert(res != nullptr);
		assert(res->status == 202); // Accepted
		auto json_body = nlohmann::json::parse(res->body);
		assert(json_body.contains("task_id"));
		task_id = json_body["task_id"].get<std::string>();
		assert(!task_id.empty());
		std::cout << "Task create OK: " << task_id << "\n";
	}

	// 5. Test GET /a2a/v1/tasks/:id
	{
		nlohmann::json json_body;
		for (int i = 0; i < 150; ++i) {
			httplib::Client poll_client(std::format("http://127.0.0.1:{}", bound_port));
			auto res = poll_client.Get(std::format("/a2a/v1/tasks/{}", task_id));
			if (res && res->status == 200) {
				json_body = nlohmann::json::parse(res->body);
				if (json_body["status"] != "running") {
					break;
				}
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
		assert(json_body["id"] == task_id);
		assert(json_body["status"] == "completed");
		std::cout << "Task status OK\n";
	}

	server.stop();
	assert(!server.is_running());

	plugin_loader::get_instance().unload_all_plugins();

	std::cout << "a2a_server unit tests passed successfully!\n";
	return 0;
}
