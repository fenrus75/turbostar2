#include <cassert>
#include <iostream>
#include <filesystem>
#include <fstream>
#include "../../src/agentlib/tool_registry.h"
#include "../../src/project_manager.h"
#include "../../src/fs_utils.h"

int main()
{
	// Initialize project manager and tool registry
	project_manager::get_instance().initialize();
	agentlib::tool_registry &registry = agentlib::tool_registry::get_instance();
	agentlib::tool_context ctx;

	std::string project_root = project_manager::get_instance().get_project_root();
	ctx.fs_security.set_working_directory(project_root);
	ctx.fs_security.add_allowed_root(project_root, agentlib::access_type::read);
	ctx.fs_security.add_allowed_root(project_root, agentlib::access_type::write);

	// Define our canary file path
	std::string canary_path = project_root + "/injection_canary_test";
	std::filesystem::remove(canary_path);

	// Verify escape_shell_arg handles single quotes correctly
	std::string raw_arg = "foo'bar";
	std::string escaped = fs_utils::escape_shell_arg(raw_arg);
	assert(escaped == "'foo'\\''bar'");

	// Construct a malicious filename containing command injection
	// We use git_add tool to test this vulnerability.
	// The path must reside inside the project root to pass fs_security validation.
	std::string malicious_file = project_root + "/safe_file'; touch " + canary_path + "; '#";

	std::cout << "Testing git_add tool with malicious path: " << malicious_file << std::endl;

	nlohmann::json args;
	args["paths"] = nlohmann::json::array({ malicious_file });

	// We prepare the tool. This will invoke git_add_validator which runs fs_security validation.
	// Since the malicious path is inside the project root and doesn't contain "..", it is validated.
	auto prep = registry.prepare_tool("git_add", args.dump(), ctx);
	assert(prep.tool != nullptr);

	// Execute the tool
	std::string result = prep.tool->execute(ctx);
	std::cout << "Tool execution result:\n" << result << std::endl;

	// Check if the canary file was created (indicating command injection succeeded)
	bool canary_created = std::filesystem::exists(canary_path);
	std::filesystem::remove(canary_path); // clean up

	if (canary_created) {
		std::cerr << "CRITICAL SECURITY VULNERABILITY: Command injection succeeded! Canary file was created." << std::endl;
		return 1;
	}

	std::cout << "Shell security unit test passed successfully!" << std::endl;
	return 0;
}
