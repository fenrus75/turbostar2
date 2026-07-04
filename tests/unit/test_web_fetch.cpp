#include "test_watchdog.h"
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <nlohmann/json.hpp>
#include <thread>
#include "agentlib/ai_agent.h"
#include "agentlib/tool_registry.h"
#include "filter_registry.h"
#include "event_queue.h"
#include "project_manager.h"
#include "fs_utils.h"

using namespace agentlib;

void write_file(const std::filesystem::path &path, const std::string &content)
{
	std::filesystem::create_directories(path.parent_path());
	std::ofstream out(path);
	out << content;
}

int main()
{
	test_watchdog::setup_watchdog(30);
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
		std::filesystem::path domains_file = std::filesystem::path(fs_utils::get_global_cache_dir()) / "allowed_domains.txt";
		write_file(domains_file, "D:blacklisted.com\n");

		nlohmann::json args = {{"url", "https://blacklisted.com/index.html"}};
		std::string result = registry.execute_tool("web_fetch", args.dump(), ctx);
		std::cout << "Result blacklisted: " << result << std::endl;
		assert(result.find("Blacklisted") != std::string::npos);
	}

	// E. Whitelisted Domain: rule is 'A' (will call curl, so we expect a connection error or success, but not blacklisted error)
	{
		std::filesystem::path domains_file = std::filesystem::path(fs_utils::get_global_cache_dir()) / "allowed_domains.txt";
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
		std::filesystem::path domains_file = std::filesystem::path(fs_utils::get_global_cache_dir()) / "allowed_domains.txt";
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
		std::filesystem::path domains_file = std::filesystem::path(fs_utils::get_global_cache_dir()) / "allowed_domains.txt";
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

	// H. silent failure check (no_ask is true)
	{
		nlohmann::json args = {{"url", "http://127.0.0.1:54321/index.html"}, {"no_ask", true}};
		std::string result = registry.execute_tool("web_fetch", args.dump(), ctx);
		std::cout << "Result with no_ask: " << result << std::endl;
		assert(result.find("silent failure") != std::string::npos);
	}

	// I. Validation failure: invalid type for no_ask (must be boolean)
	{
		nlohmann::json args = {{"url", "http://127.0.0.1:54321/index.html"}, {"no_ask", 123}};
		auto prep = registry.prepare_tool("web_fetch", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	// J. output_path verification (validation, security, and writing output to disk)
	{
		// 1. Invalid type for output_path
		{
			nlohmann::json args = {{"url", "http://127.0.0.1:54321/index.html"}, {"output_path", 123}};
			auto prep = registry.prepare_tool("web_fetch", args.dump(), ctx);
			assert(prep.tool == nullptr);
			assert(!prep.error_message.empty());
		}

		// 2. output_path outside workspace (security boundary check)
		{
			nlohmann::json args = {{"url", "http://127.0.0.1:54321/index.html"}, {"output_path", "../outside.txt"}};
			auto prep = registry.prepare_tool("web_fetch", args.dump(), ctx);
			assert(prep.tool == nullptr);
			assert(prep.error_message.find("Security Violation") != std::string::npos);
		}

		// 3. Successful download write to disk
		{
			std::string out_fn = "web_fetch_test_out.txt";
			std::string full_out_fn = project_manager::get_instance().get_project_root() + "/" + out_fn;
			if (std::filesystem::exists(full_out_fn)) {
				std::filesystem::remove(full_out_fn);
			}

			std::filesystem::path domains_file = std::filesystem::path(fs_utils::get_global_cache_dir()) / "allowed_domains.txt";
			write_file(domains_file, "A:127.0.0.1\n");

			nlohmann::json args = {{"url", "http://127.0.0.1:54321/index.html"}, {"output_path", out_fn}};
			std::string result = registry.execute_tool("web_fetch", args.dump(), ctx);
			std::cout << "Result download execution: " << result << std::endl;
			assert(result.find("Success: Downloaded") != std::string::npos);
			assert(result.find(out_fn) != std::string::npos);

			// Verify file was written and is not empty
			assert(std::filesystem::exists(full_out_fn));
			std::ifstream ifs(full_out_fn);
			std::string file_content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
			ifs.close();
			assert(!file_content.empty());

			// Clean up
			std::filesystem::remove(full_out_fn);
		}

		// 4. Test filter registration and application
		{
			filter_registry::get_instance().register_filter("uppercase", [](const std::string &input) {
				std::string result = input;
				std::transform(result.begin(), result.end(), result.begin(), ::toupper);
				return result;
			});

			std::filesystem::path domains_file = std::filesystem::path(fs_utils::get_global_cache_dir()) / "allowed_domains.txt";
			write_file(domains_file, "A:127.0.0.1\n");

			nlohmann::json args = {{"url", "http://127.0.0.1:54321/index.html"}, {"filter", "uppercase"}};
			std::string result = registry.execute_tool("web_fetch", args.dump(), ctx);
			std::cout << "Result filter execution: " << result << std::endl;
			assert(result.find("COULD NOT CONNECT") != std::string::npos || result.find("COULDNT_CONNECT") != std::string::npos);

			filter_registry::get_instance().unregister_filter("uppercase");
		}

		// 5. Test unregistered filter failure
		{
			nlohmann::json args = {{"url", "http://127.0.0.1:54321/index.html"}, {"filter", "nonexistent_filter"}};
			std::string result = registry.execute_tool("web_fetch", args.dump(), ctx);
			std::cout << "Result unregistered filter: " << result << std::endl;
			assert(result.find("Error: Filter 'nonexistent_filter' is not registered") != std::string::npos);
		}

		// 6. Verify pre-registered core filters
		{
			bool success = false;
			std::string res = filter_registry::get_instance().apply_filter("markdown_align_tables", "| A | B |\n|---|---|\n| 1 | 2 |", success);
			assert(success);
			assert(res.find("| A | B |") != std::string::npos);

			res = filter_registry::get_instance().apply_filter("meson_compile", "[1/3] Compiling foo.cpp\n[2/3] Compiling bar.cpp\n", success);
			assert(success);
			assert(res.find("bar.cpp") != std::string::npos);
			assert(res.find("foo.cpp") == std::string::npos);

			res = filter_registry::get_instance().apply_filter("meson_test", " 1/66 unit_event_logger                OK              0.01s\n 2/66 unit_history_manager             OK              0.01s\n", success);
			assert(success);
			assert(res.find("unit_history_manager") != std::string::npos);

			res = filter_registry::get_instance().apply_filter("strip_ansi", "\x1b[31mHello\x1b[0m World", success);
			assert(success);
			assert(res == "Hello World");
		}
	}

	// Clean up
	std::filesystem::remove_all(temp_home);
	std::cout << "web_fetch tests passed successfully.\n";
	return 0;
}
