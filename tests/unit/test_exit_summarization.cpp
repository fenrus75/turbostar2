#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "../../src/agentlib/ai_model.h"
#include "../../src/agentlib/document_provider.h"
#include "../../src/agentlib/interactions/interactions.h"
#include "../../src/agentlib/llm_client.h"
#include "../../src/agentlib/tool_registry.h"

#include "../../src/config_manager.h"
#include "../../src/event_logger.h"
#include "../../src/event_queue.h"
#include "../../src/fs_utils.h"
#include "../../src/project_manager.h"

#define private public
#include "../../src/agentlib/ai_agent.h"
#undef private

using namespace agentlib;

int main()
{
	event_logger::get_instance().enable_stdout_logging(true);
	project_manager::get_instance().initialize();

	std::string history_dir = fs_utils::get_project_history_dir("TestAgentExit");
	if (std::filesystem::exists(history_dir)) {
		std::filesystem::remove_all(history_dir);
	}

	event_queue q;
	auto model = std::make_shared<ai_model>("test-model", "Test Model", "http://localhost:1", "Test", 0.0, 0.0);
	ai_model_registry::get_instance().register_model(model);
	config_manager::get_instance().set_default_model_id("test-model");

	// 1. First test: with exiting = false
	project_manager::get_instance().set_exiting(false);
	{
		auto agent = ai_agent::create(1, "TestAgentExit", model, &q, nullptr);

		// Inject some context so it pages out
		agent->inject_context("user", "Hello");
		agent->inject_context("assistant", std::string(1100, 'x'));
		agent->inject_context("user", "More text");

		// Page out to trigger queuing a task
		agent->page_out_context(0, 2, "Test Segment", "test summary", {"test"});

		std::cout << "Initial summary queue size: " << agent->summary_queue_.size() << std::endl;
	}

	// 2. Second test: with exiting = true
	project_manager::get_instance().set_exiting(true);
	{
		auto agent = ai_agent::create(2, "TestAgentExit", model, &q, nullptr);

		// Inject some context so it pages out
		agent->inject_context("user", "Hello");
		agent->inject_context("assistant", std::string(1100, 'x'));
		agent->inject_context("user", "More text");

		// Page out when exiting is true
		agent->page_out_context(0, 2, "Test Segment", "test summary", {"test"});

		// The queue should definitely be empty because the is_exiting() check prevents pushing!
		std::cout << "Summary queue size when exiting: " << agent->summary_queue_.size() << std::endl;
		assert(agent->summary_queue_.empty());
	}

	// 3. Third test: exit during an active HTTP request
	{
		struct SlowServer {
			int server_fd = -1;
			int client_fd = -1;
			std::thread th;
			std::atomic<bool> stop_flag{false};
			int port = 0;

			SlowServer() {
				server_fd = socket(AF_INET, SOCK_STREAM, 0);
				assert(server_fd >= 0);
				int opt = 1;
				setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
				sockaddr_in address{};
				address.sin_family = AF_INET;
				address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
				address.sin_port = htons(0); // Auto-assign free port
				int rc = bind(server_fd, (struct sockaddr*)&address, sizeof(address));
				assert(rc >= 0);
				rc = listen(server_fd, 3);
				assert(rc >= 0);

				socklen_t len = sizeof(address);
				getsockname(server_fd, (struct sockaddr*)&address, &len);
				port = ntohs(address.sin_port);

				th = std::thread([this]() {
					while (!stop_flag) {
						struct sockaddr_in client_addr{};
						socklen_t addrlen = sizeof(client_addr);
						int fd = accept(server_fd, (struct sockaddr*)&client_addr, &addrlen);
						if (fd >= 0) {
							client_fd = fd;
							while (!stop_flag) {
								std::this_thread::sleep_for(std::chrono::milliseconds(50));
							}
							close(client_fd);
							client_fd = -1;
						}
					}
				});
			}

			~SlowServer() {
				stop_flag = true;
				if (server_fd >= 0) {
					shutdown(server_fd, SHUT_RDWR);
					close(server_fd);
				}
				if (client_fd >= 0) {
					shutdown(client_fd, SHUT_RDWR);
					close(client_fd);
				}
				if (th.joinable()) {
					th.join();
				}
			}
		};

		std::cout << "Starting SlowServer..." << std::endl;
		SlowServer server;
		std::string server_url = "http://localhost:" + std::to_string(server.port);
		std::cout << "SlowServer listening on: " << server_url << std::endl;

		auto slow_model = std::make_shared<ai_model>("test-slow-model", "Test Slow Model", server_url, "Test", 0.0, 0.0);
		ai_model_registry::get_instance().register_model(slow_model);
		config_manager::get_instance().set_default_model_id("test-slow-model");

		project_manager::get_instance().set_exiting(false);

		auto start_time = std::chrono::steady_clock::now();
		{
			auto agent = ai_agent::create(3, "TestAgentSlow", slow_model, &q, nullptr);

			// Inject some context so it pages out
			agent->inject_context("user", "Hello");
			agent->inject_context("assistant", std::string(1100, 'x'));
			agent->inject_context("user", "More text");

			// Page out to trigger queuing a task
			agent->page_out_context(0, 2, "Test Segment", "test summary", {"test"});

			// Wait briefly so the background summary thread starts processing, connects to our server,
			// and hangs there inside send_chat/Post
			std::this_thread::sleep_for(std::chrono::milliseconds(200));

			// Now let's destruct the agent. It should cancel the background request and exit immediately.
			std::cout << "Destructing agent with active background task..." << std::endl;
		}
		auto end_time = std::chrono::steady_clock::now();
		auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
		std::cout << "Agent destruction took: " << duration_ms << " ms" << std::endl;

		assert(duration_ms < 1500);
	}

	std::cout << "test_exit_summarization passed successfully!" << std::endl;
	return 0;
}
