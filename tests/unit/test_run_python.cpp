#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/project_manager.h"

using namespace agentlib;

void write_file(const std::filesystem::path &path, const std::string &content)
{
	std::filesystem::create_directories(path.parent_path());
	std::ofstream out(path);
	out << content;
}

int main()
{
	project_manager::get_instance().initialize();

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;

	std::string project_root = project_manager::get_instance().get_project_root();
	ctx.fs_security.set_working_directory(project_root);
	ctx.fs_security.add_allowed_root(project_root, access_type::read);
	ctx.fs_security.add_allowed_root(project_root, access_type::write);

	std::cout << "Testing run_python..." << std::endl;

	// 1. Success case: execute code directly
	{
		nlohmann::json args = {{"code", "print('Hello from inline Python!')"}};
		std::string result = registry.execute_tool("run_python", args.dump(), ctx);
		std::cout << "Result: " << result << std::endl;
		assert(result.find("Hello from inline Python!") != std::string::npos);
	}

	// 2. Success case: execute python script from file
	{
		std::filesystem::path script_path = std::filesystem::path(project_root) / "test_run_python_temp.py";
		write_file(script_path, "print('Hello from script file!')\n");

		nlohmann::json args = {{"file_path", "test_run_python_temp.py"}};
		std::string result = registry.execute_tool("run_python", args.dump(), ctx);
		std::cout << "Result: " << result << std::endl;
		assert(result.find("Hello from script file!") != std::string::npos);

		std::filesystem::remove(script_path);
	}

	// 3. Validation failure: both code and file_path provided
	{
		nlohmann::json args = {{"code", "print(1)"}, {"file_path", "test.py"}};
		auto prep = registry.prepare_tool("run_python", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(prep.error_message.find("Cannot provide both") != std::string::npos);
	}

	// 4. Validation failure: neither code nor file_path provided
	{
		nlohmann::json args = nlohmann::json::object();
		auto prep = registry.prepare_tool("run_python", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(prep.error_message.find("Must provide exactly one") != std::string::npos);
	}

	// 5. Validation failure: file_path does not exist
	{
		nlohmann::json args = {{"file_path", "non_existent_file_xyz_123.py"}};
		auto prep = registry.prepare_tool("run_python", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(prep.error_message.find("does not exist") != std::string::npos);
	}

	// 6. Security failure: file_path outside allowed root
	{
		nlohmann::json args = {{"file_path", "../../../etc/passwd"}};
		auto prep = registry.prepare_tool("run_python", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(prep.error_message.find("Security Violation") != std::string::npos ||
		       prep.error_message.find("Access denied") != std::string::npos);
	}

	// 7. Validation failure: unexpected arguments (should fail validation as per review recommendations)
	{
		nlohmann::json args = {{"code", "print(1)"}, {"unexpected_arg", 123}};
		auto prep = registry.prepare_tool("run_python", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	// 8. Bandit security check case (inline code)
	bool bandit_installed = (std::system("which bandit > /dev/null 2>&1") == 0);
	if (bandit_installed) {
		std::cout << "Testing bandit validation with insecure inline code..." << std::endl;
		nlohmann::json args = {{"code", "import subprocess\ndef run_user_command():\n    user_input = input(\"Enter a command to run: \")\n    subprocess.call(user_input, shell=True)\nrun_user_command()"}};
		std::string result = registry.execute_tool("run_python", args.dump(), ctx);
		assert(result.find("Security Validation Failed") != std::string::npos);
	}

	// 9. Bandit security check case (file path)
	if (bandit_installed) {
		std::cout << "Testing bandit validation with insecure script file..." << std::endl;
		std::filesystem::path script_path = std::filesystem::path(project_root) / "test_run_python_insecure.py";
		write_file(script_path, "import subprocess\ndef run_user_command():\n    user_input = input(\"Enter a command to run: \")\n    subprocess.call(user_input, shell=True)\nrun_user_command()\n");

		nlohmann::json args = {{"file_path", "test_run_python_insecure.py"}};
		std::string result = registry.execute_tool("run_python", args.dump(), ctx);
		assert(result.find("Security Validation Failed") != std::string::npos);

		std::filesystem::remove(script_path);
	}

	std::cout << "run_python tests passed successfully.\n";
	return 0;
}
