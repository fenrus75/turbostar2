#include <cassert>
#include <iostream>
#include <nlohmann/json.hpp>
#include "../../src/agentlib/virtual_file_system.h"

using namespace agentlib;

void test_uri_parsing_and_routing()
{
	virtual_file_system vfs;

	// Verify we can route github paths and they exist/don't exist correctly
	// If rate limits or network issues occur, get_file_info/read_file will return nullopt
	// rather than crashing.

	// Test user-only VFS URI
	std::string user_uri = "github://octocat";
	auto user_info = vfs.get_file_info(user_uri);
	assert(user_info.has_value());
	assert(user_info->type == 'D');

	// Test repo root VFS URI
	std::string repo_uri = "github://octocat/Hello-World";
	auto repo_info = vfs.get_file_info(repo_uri);
	assert(repo_info.has_value());
	assert(repo_info->type == 'D');

	// Test repo root with branch
	std::string repo_branch_uri = "github://octocat/Hello-World@master";
	auto repo_branch_info = vfs.get_file_info(repo_branch_uri);
	assert(repo_branch_info.has_value());
	assert(repo_branch_info->type == 'D');
}

void test_live_network_fetch_or_graceful_failure()
{
	virtual_file_system vfs;

	// Try reading a known raw file
	std::string file_uri = "github://octocat/Hello-World/README";
	auto handle_opt = vfs.read_file(file_uri);

	if (handle_opt.has_value()) {
		std::string_view content = handle_opt.value()->view();
		std::cout << "Successfully fetched README content (length: " << content.length() << ")\n";
		assert(content.find("Hello World") != std::string_view::npos);

		// Test cache hit
		auto handle_opt2 = vfs.read_file(file_uri);
		assert(handle_opt2.has_value());
		assert(handle_opt2.value()->view() == content);
	} else {
		std::cout << "Fetch returned nullopt (expected if rate-limited or offline).\n";
	}
}

void test_directory_listing_or_graceful_failure()
{
	virtual_file_system vfs;

	std::string user_uri = "github://octocat/";
	auto list = vfs.list_directory(user_uri);

	if (!list.empty()) {
		std::cout << "Successfully retrieved repo list (size: " << list.size() << ")\n";
		bool found_hello = false;
		for (const auto &item : list) {
			if (item.uri.find("Hello-World") != std::string::npos) {
				found_hello = true;
				break;
			}
		}
		assert(found_hello);
	} else {
		std::cout << "Repo list returned empty (expected if rate-limited or offline).\n";
	}

	// Test case requested by user: fenrus75/powertop (with explicit branch)
	std::string powertop_branch_uri = "github://fenrus75/powertop@master/";
	auto powertop_branch_list = vfs.list_directory(powertop_branch_uri);
	if (!powertop_branch_list.empty()) {
		std::cout << "Successfully retrieved powertop@master list (size: " << powertop_branch_list.size() << ")\n";
		bool found_src = false;
		for (const auto &item : powertop_branch_list) {
			if (item.uri.find("/src") != std::string::npos) {
				found_src = true;
				break;
			}
		}
		assert(found_src);
	} else {
		std::cout << "Powertop@master list returned empty (expected if rate-limited or offline).\n";
	}

	// Test case requested by user: fenrus75/powertop/ (relying on default branch resolution)
	std::string powertop_uri = "github://fenrus75/powertop/";
	auto powertop_list = vfs.list_directory(powertop_uri);
	if (!powertop_list.empty()) {
		std::cout << "Successfully retrieved powertop list (size: " << powertop_list.size() << ")\n";
		bool found_src = false;
		for (const auto &item : powertop_list) {
			if (item.uri.find("/src") != std::string::npos) {
				found_src = true;
				break;
			}
		}
		assert(found_src);
	} else {
		std::cout << "Powertop list returned empty (expected if rate-limited or offline).\n";
	}
}

int main()
{
	std::cout << "Running github_vfs tests...\n";
	test_uri_parsing_and_routing();
	test_live_network_fetch_or_graceful_failure();
	test_directory_listing_or_graceful_failure();
	std::cout << "github_vfs tests passed.\n";
	return 0;
}
