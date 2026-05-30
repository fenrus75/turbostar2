#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <future>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <nlohmann/json.hpp>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/project_manager.h"
#include "../../src/event_queue.h"

using namespace agentlib;

void write_file(const std::filesystem::path &path, const std::string &content)
{
	std::filesystem::create_directories(path.parent_path());
	std::ofstream out(path);
	out << content;
}

int main()
{
	// 1. Create a temporary home directory
	std::filesystem::path temp_home = std::filesystem::absolute("./test_web_fetch_home");
	if (std::filesystem::exists(temp_home)) {
		std::filesystem::remove_all(temp_home);
	}
	std::filesystem::create_directories(temp_home);
	setenv("HOME", temp_home.c_str(), 1);

	project_manager::get_instance().initialize();

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;
	event_queue q;
	ctx.queue = &q;

	ctx.fs_security.set_working_directory(project_manager::get_instance().get_project_root());
	ctx.fs_security.add_allowed_root(project_manager::get_instance().get_project_root(), access_type::read);
	ctx.fs_security.add_allowed_root(project_manager::get_instance().get_project_root(), access_type::write);

	std::cout << "Testing web_fetch..." << std::endl;

	// A. Validation failure: missing parameter 'url'
	{
		nlohmann::json args = nlohmann::json::object();
		auto prep = registry.prepare_tool("web_fetch", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	// B. Validation failure: invalid URL scheme (not http/https)
	{
		nlohmann::json args = {{"url", "ftp://example.com"}};
		auto prep = registry.prepare_tool("web_fetch", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(prep.error_message.find("URL must start with") != std::string::npos);
	}

	// C. Validation failure: unexpected arguments (should fail validation as per review recommendations)
	{
		nlohmann::json args = {{"url", "https://example.com"}, {"unexpected_arg", 123}};
		auto prep = registry.prepare_tool("web_fetch", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	// D. Blacklisted Domain: rule is 'D'
	{
		std::filesystem::path domains_file = temp_home / ".cache" / "turbostar" / "allowed_domains.txt";
		write_file(domains_file, "D:blacklisted.com\n");

		nlohmann::json args = {{"url", "https://blacklisted.com/index.html"}};
		std::string result = registry.execute_tool("web_fetch", args.dump(), ctx);
		std::cout << "Result blacklisted: " << result << std::endl;
		assert(result.find("Blacklisted") != std::string::npos);
	}

	// E. Whitelisted Domain: rule is 'A' (will call curl, so we expect a connection error or success, but not blacklisted error)
	{
		std::filesystem::path domains_file = temp_home / ".cache" / "turbostar" / "allowed_domains.txt";
		write_file(domains_file, "A:127.0.0.1\n");

		nlohmann::json args = {{"url", "http://127.0.0.1:54321/index.html"}};
		std::string result = registry.execute_tool("web_fetch", args.dump(), ctx);
		std::cout << "Result whitelisted curl execution: " << result << std::endl;
		assert(result.find("Blacklisted") == std::string::npos);
		assert(result.find("Permission denied") == std::string::npos);
	}

	// F. Prompts user: local IP (Deny Always)
	{
		// Clean up domains file
		std::filesystem::path domains_file = temp_home / ".cache" / "turbostar" / "allowed_domains.txt";
		if (std::filesystem::exists(domains_file)) {
			std::filesystem::remove(domains_file);
		}

		std::thread worker([&q]() {
			while (true) {
				auto ev = q.pop();
				if (ev) {
					if (ev->type == event_type::prompt_user) {
						assert(ev->payload.find("Allow connection to 127.0.0.1?") != std::string::npos);
						ev->prompt_promise->set_value("Deny Always");
						break;
					}
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
		});

		nlohmann::json args = {{"url", "http://127.0.0.1:54321/index.html"}};
		std::string result = registry.execute_tool("web_fetch", args.dump(), ctx);
		worker.join();

		std::cout << "Result Deny Always: " << result << std::endl;
		assert(result.find("Blacklisted") != std::string::npos);

		// Verify written rule
		std::ifstream in(domains_file);
		std::string line;
		bool found_deny = false;
		while (std::getline(in, line)) {
			if (line == "D:127.0.0.1") {
				found_deny = true;
			}
		}
		assert(found_deny);
	}

	// G. Prompts user: Once
	{
		// Clean up domains file
		std::filesystem::path domains_file = temp_home / ".cache" / "turbostar" / "allowed_domains.txt";
		if (std::filesystem::exists(domains_file)) {
			std::filesystem::remove(domains_file);
		}

		std::thread worker([&q]() {
			while (true) {
				auto ev = q.pop();
				if (ev) {
					if (ev->type == event_type::prompt_user) {
						ev->prompt_promise->set_value("Once");
						break;
					}
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
		});

		nlohmann::json args = {{"url", "http://127.0.0.1:54321/index.html"}};
		std::string result = registry.execute_tool("web_fetch", args.dump(), ctx);
		worker.join();

		std::cout << "Result Once: " << result << std::endl;
		assert(result.find("Blacklisted") == std::string::npos);
	}

	// Clean up
	std::filesystem::remove_all(temp_home);
	std::cout << "web_fetch tests passed successfully.\n";
	return 0;
}
