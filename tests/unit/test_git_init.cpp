#include <cassert>
#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/project_manager.h"

using namespace agentlib;

int main()
{
	project_manager::get_instance().initialize();

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;

	std::string project_root = project_manager::get_instance().get_project_root();
	ctx.fs_security.set_working_directory(project_root);
	ctx.fs_security.add_allowed_root(project_root, access_type::read);
	ctx.fs_security.add_allowed_root(project_root, access_type::write);

	std::cout << "Testing git_init..." << std::endl;

	std::filesystem::path git_path = std::filesystem::path(project_root) / ".git";
	std::filesystem::path git_backup = std::filesystem::path(project_root) / ".git_backup";

	// 1. Success case and Unexpected Parameter validation (when .git does not exist)
	if (std::filesystem::exists(git_path)) {
		std::filesystem::rename(git_path, git_backup);
	}

	try {
		// A. Success path
		std::string result = registry.execute_tool("git_init", "{}", ctx);
		std::cout << "Success path result: " << result << std::endl;
		assert(result.find("Successfully initialized") != std::string::npos);
		assert(std::filesystem::exists(git_path));

		// Remove the newly created .git dir
		std::filesystem::remove_all(git_path);

		// B. Validation failure: unexpected arguments (should fail validation as per review recommendations)
		{
			nlohmann::json args = {{"unexpected_arg", 123}};
			auto prep = registry.prepare_tool("git_init", args.dump(), ctx);
			if (prep.tool != nullptr) {
				// Restore backup first
				if (std::filesystem::exists(git_backup)) {
					if (std::filesystem::exists(git_path)) {
						std::filesystem::remove_all(git_path);
					}
					std::filesystem::rename(git_backup, git_path);
				}
				std::cerr << "Assertion failed: prep.tool == nullptr (unexpected arguments not rejected)" << std::endl;
				exit(1);
			}
		}
	} catch (...) {
		if (std::filesystem::exists(git_backup)) {
			if (std::filesystem::exists(git_path)) {
				std::filesystem::remove_all(git_path);
			}
			std::filesystem::rename(git_backup, git_path);
		}
		throw;
	}

	// Restore backup
	if (std::filesystem::exists(git_backup)) {
		std::filesystem::rename(git_backup, git_path);
	}

	// 2. Failure case: .git already exists in project root
	{
		auto prep = registry.prepare_tool("git_init", "{}", ctx);
		assert(prep.tool == nullptr);
		assert(prep.error_message.find("already exists") != std::string::npos);
	}

	std::cout << "git_init tests passed successfully.\n";
	return 0;
}
