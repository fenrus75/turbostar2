#include <cassert>
#include <iostream>
#include "a2a/a2a_client.h"
#include "a2a/a2a_server.h"
#include "a2a/a2a_server_manager.h"
#include "fs_utils.h"
#include "project_manager.h"
#include "test_watchdog.h"

#include "agentlib/subagent_manager.h"

using namespace a2a;

int main()
{
	test_watchdog::setup_watchdog(30);

	std::filesystem::path temp_home = std::filesystem::absolute("./test_a2a_client_home");
	if (std::filesystem::exists(temp_home)) {
		std::filesystem::remove_all(temp_home);
	}
	std::filesystem::create_directories(temp_home);
	setenv("HOME", temp_home.c_str(), 1);

	project_manager::get_instance().initialize();
	agentlib::subagent_manager::get_instance().initialize();

	std::cout << "Testing a2a_server_manager..." << std::endl;
	{
		auto &mgr = a2a_server_manager::get_instance();
		mgr.clear_ephemeral_servers();

		// Add global, project, and ephemeral servers
		a2a_server_config s_global;
		s_global.name = "devpc";
		s_global.url = "http://devpc.global:7820";
		s_global.tier = a2a_server_tier::global_system;
		mgr.add_server(s_global);

		a2a_server_config s_proj;
		s_proj.name = "devpc";
		s_proj.url = "http://devpc.project:7820";
		s_proj.tier = a2a_server_tier::project_local;
		mgr.add_server(s_proj);

		// Project priority over Global
		auto found = mgr.find_server("devpc");
		assert(found.has_value());
		assert(found->url == "http://devpc.project:7820");

		// Ephemeral priority over Project
		a2a_server_config s_eph;
		s_eph.name = "devpc";
		s_eph.url = "http://devpc.ephemeral:7820";
		s_eph.tier = a2a_server_tier::ephemeral_runtime;
		mgr.add_server(s_eph);

		found = mgr.find_server("devpc");
		assert(found.has_value());
		assert(found->url == "http://devpc.ephemeral:7820");

		// Add distinct server
		a2a_server_config s_remote;
		s_remote.name = "gpu_box";
		s_remote.url = "http://gpu.local:9000";
		s_remote.tier = a2a_server_tier::global_system;
		mgr.add_server(s_remote);

		auto all = mgr.get_all_servers();
		assert(all.size() == 2);

		// Test persistence save/load
		mgr.save_global_servers();
		mgr.save_project_servers();

		mgr.remove_server("devpc");
		mgr.remove_server("gpu_box");
		assert(mgr.get_all_servers().empty());

		mgr.load_global_servers();
		mgr.load_project_servers();
		all = mgr.get_all_servers();
		assert(all.size() == 2);

		std::cout << "a2a_server_manager verified successfully!" << std::endl;
	}

	std::cout << "Testing a2a_client with local a2a_server..." << std::endl;
	{
		int bound_port = 0;
		bool started = a2a_server::get_instance().start(7850, &bound_port);
		assert(started);
		assert(bound_port > 0);

		std::string server_url = std::format("http://127.0.0.1:{}", bound_port);
		auto &client = a2a_client::get_instance();

		// 1. Test fetch_agent_card
		std::string err;
		auto card = client.fetch_agent_card(server_url, err);
		assert(card.has_value());
		assert(!card->name.empty());
		std::cout << "Fetched Agent Card Name: " << card->name << std::endl;

		// 2. Test submit_task
		auto submit_res = client.submit_task(server_url, "research", "Analyze project structure");
		assert(submit_res.success);
		assert(!submit_res.task_id.empty());
		std::cout << "Task submitted successfully. ID: " << submit_res.task_id << std::endl;

		// 3. Test poll_task
		auto poll_res = client.poll_task(server_url, submit_res.task_id);
		assert(!poll_res.status.empty());
		std::cout << "Polled task status: " << poll_res.status << std::endl;

		// 4. Test cancel_task
		bool canceled = client.cancel_task(server_url, submit_res.task_id);
		assert(canceled);
		std::cout << "Task canceled successfully." << std::endl;

		a2a_server::get_instance().stop();
		std::cout << "a2a_client verified successfully!" << std::endl;
	}

	return 0;
}
