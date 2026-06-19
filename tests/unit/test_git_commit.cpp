#include "test_watchdog.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/fs_utils.h"
#include "../../src/project_manager.h"
#include "../../src/codereview_manager.h"
#include "git_test_helper.h"

using namespace agentlib;

void write_file(const std::filesystem::path &path, const std::string &content)
{
	if (!path.parent_path().empty()) {
		std::filesystem::create_directories(path.parent_path());
	}
	std::ofstream out(path);
	out << content;
}

int main()
{
	test_watchdog::setup_watchdog(30);
	project_manager::get_instance().initialize();

	temp_git_repo repo("commit");

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;
	ctx.fs_security.set_working_directory(repo.get_path());
	ctx.fs_security.add_allowed_root(repo.get_path(), access_type::read);
	ctx.fs_security.add_allowed_root(repo.get_path(), access_type::write);

	std::cout << "Testing git_commit..." << std::endl;

	// 1. Failure case: nothing to commit
	{
		nlohmann::json args = {{"message", "should fail because nothing is staged"}};
		std::string result = registry.execute_tool("git_commit", args.dump(), ctx);
		std::cout << "Result nothing staged: " << result << std::endl;
		assert(result.find("No staged changes found") != std::string::npos);
	}

	// 2. Success case: stage a file, commit it
	{
		std::string test_dir = repo.get_path();
		std::filesystem::path dummy_file = std::filesystem::path(test_dir) / "dummy_commit_test.txt";
		write_file(dummy_file, "dummy content modified");
		fs_utils::execute_command_sync("git -C {} add dummy_commit_test.txt", test_dir);

		nlohmann::json args = {{"message", "test: dummy commit for unit test"}};
		std::string result = registry.execute_tool("git_commit", args.dump(), ctx);
		std::cout << "Result success commit: " << result << std::endl;
		assert(result.find("Successfully created commit") != std::string::npos);
	}

	// 3. Validation failure: empty commit message
	{
		nlohmann::json args = {{"message", ""}};
		auto prep = registry.prepare_tool("git_commit", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	// 4. Validation failure: unexpected arguments (should fail validation as per review recommendations)
	{
		nlohmann::json args = {{"message", "test msg"}, {"unexpected_arg", 123}};
		auto prep = registry.prepare_tool("git_commit", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	// 5. Outstanding code reviews reminder in commit response
	{
		std::string test_dir = repo.get_path();
		codereview_manager &manager = codereview_manager::get_instance();
		manager.load_project(test_dir);
		manager.clear_all();
		
		int item_id = manager.create_code_review_item("Fix null deref", "dummy_commit_test.txt", 10, "int* p = nullptr;", "high",
							      "Null pointer dereference", "Check null");
		assert(item_id >= 0);
		
		std::filesystem::path dummy_file = std::filesystem::path(test_dir) / "dummy_commit_test.txt";
		write_file(dummy_file, "dummy content modified again");
		fs_utils::execute_command_sync("git -C {} add dummy_commit_test.txt", test_dir);

		nlohmann::json args = {{"message", "test: fix outstanding issue"}};
		std::string result = registry.execute_tool("git_commit", args.dump(), ctx);
		std::cout << "Result commit with outstanding items:\n" << result << std::endl;
		
		assert(result.find("Successfully created commit") != std::string::npos);
		assert(result.find("Outstanding Code Review Items Reminder") != std::string::npos);
		assert(result.find("Fix null deref") != std::string::npos);

		// Get the actual commit hash in test repo to verify it is matched in the message
		std::string expected_hash = fs_utils::execute_command_sync("git -C {} rev-parse HEAD", test_dir);
		std::stringstream hash_ss(expected_hash);
		std::string first_line;
		if (std::getline(hash_ss, first_line)) {
			while (!first_line.empty() && (first_line.back() == '\r' || first_line.back() == '\n' || std::isspace(first_line.back()))) {
				first_line.pop_back();
			}
		}
		assert(!first_line.empty());
		assert(result.find(std::format("(hash: {})", first_line)) != std::string::npos);
		
		// Clean up codereview database
		manager.clear_all();
	}

	// 6. Outstanding code reviews limit (max 10) in commit response
	{
		std::string test_dir = repo.get_path();
		codereview_manager &manager = codereview_manager::get_instance();
		manager.load_project(test_dir);
		manager.clear_all();
		
		for (int i = 1; i <= 12; ++i) {
			int item_id = manager.create_code_review_item(std::format("Fix issue {}", i), "dummy_commit_test.txt", i, "dummy line", "high",
								      "issue description", "check issue");
			assert(item_id >= 0);
		}
		
		std::filesystem::path dummy_file = std::filesystem::path(test_dir) / "dummy_commit_test.txt";
		write_file(dummy_file, "dummy content modified for limit test");
		fs_utils::execute_command_sync("git -C {} add dummy_commit_test.txt", test_dir);

		nlohmann::json args = {{"message", "test: fix outstanding issues limit"}};
		std::string result = registry.execute_tool("git_commit", args.dump(), ctx);
		std::cout << "Result commit with 12 outstanding items:\n" << result << std::endl;
		
		assert(result.find("Successfully created commit") != std::string::npos);
		assert(result.find("Outstanding Code Review Items Reminder") != std::string::npos);
		assert(result.find("Fix issue 1") != std::string::npos);
		assert(result.find("Fix issue 10") != std::string::npos);
		assert(result.find("... and 2 more outstanding items.") != std::string::npos);
		assert(result.find("Fix issue 11") == std::string::npos);
		assert(result.find("Fix issue 12") == std::string::npos);
		
		// Clean up codereview database
		manager.clear_all();
	}

	std::cout << "git_commit tests passed successfully.\n";
	return 0;
}
