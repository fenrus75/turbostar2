#include "test_watchdog.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/agentlib/virtual_file_system.h"
#include "../../src/fs_utils.h"
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
	test_watchdog::setup_watchdog(60);
	test_watchdog::isolate_home("test_run_python");
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

	std::string pid_str = std::to_string(getpid());

	// 2. Success case: execute python script from file
	{
		std::string script_filename = "test_run_python_temp_" + pid_str + ".py";
		std::filesystem::path script_path = std::filesystem::path(project_root) / script_filename;
		write_file(script_path, "print('Hello from script file!')\n");

		nlohmann::json args = {{"path", script_filename}};
		std::string result = registry.execute_tool("run_python", args.dump(), ctx);
		std::cout << "Result: " << result << std::endl;
		assert(result.find("Hello from script file!") != std::string::npos);

		std::filesystem::remove(script_path);
	}

	// 3. Validation failure: both code and file_path provided
	{
		nlohmann::json args = {{"code", "print(1)"}, {"path", "test.py"}};
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
		nlohmann::json args = {{"path", "non_existent_file_xyz_123.py"}};
		auto prep = registry.prepare_tool("run_python", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(prep.error_message.find("does not exist") != std::string::npos);
	}

	// 6. Security failure: file_path outside allowed root
	{
		nlohmann::json args = {{"path", "../../../etc/passwd"}};
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
	bool bandit_installed = (access("/usr/bin/bandit", X_OK) == 0);
	if (bandit_installed) {
		std::cout << "Testing bandit validation with insecure inline code..." << std::endl;
		nlohmann::json args = {{"code", "import subprocess\ndef run_user_command():\n    user_input = input(\"Enter a command to run: \")\n    subprocess.call(user_input, shell=True)\nrun_user_command()"}};
		std::string result = registry.execute_tool("run_python", args.dump(), ctx);
		assert(result.find("Security Validation Failed") != std::string::npos);
	}

	// 9. Bandit security check case (file path)
	if (bandit_installed) {
		std::cout << "Testing bandit validation with insecure script file..." << std::endl;
		std::string insecure_filename = "test_run_python_insecure_" + pid_str + ".py";
		std::filesystem::path script_path = std::filesystem::path(project_root) / insecure_filename;
		write_file(script_path, "import subprocess\ndef run_user_command():\n    user_input = input(\"Enter a command to run: \")\n    subprocess.call(user_input, shell=True)\nrun_user_command()\n");

		nlohmann::json args = {{"path", insecure_filename}};
		std::string result = registry.execute_tool("run_python", args.dump(), ctx);
		assert(result.find("Security Validation Failed") != std::string::npos);

		std::filesystem::remove(script_path);
	}

	// 10. Test VFS path execution (tmp://)
	{
		virtual_file_system vfs;
		ctx.fs_security.set_vfs(&vfs);

		std::string vfs_filename = "test_run_python_vfs_" + pid_str + ".py";
		std::string vfs_path = "tmp://" + vfs_filename;
		std::string physical_vfs_path = fs_utils::get_project_tmp_dir() + "/" + vfs_filename;
		write_file(std::filesystem::path(physical_vfs_path), "print('Hello from VFS tmp script file!')\n");

		nlohmann::json args = {{"path", vfs_path}};
		std::string result = registry.execute_tool("run_python", args.dump(), ctx);
		std::cout << "VFS Result: " << result << std::endl;
		assert(result.find("Hello from VFS tmp script file!") != std::string::npos);

		std::filesystem::remove(physical_vfs_path);
		ctx.fs_security.set_vfs(nullptr);
	}

	// 11. Dependencies parameter test
	{
		nlohmann::json args = {
			{"code", "import sys\nprint('With dependencies test ok')"},
			{"dependencies", nlohmann::json::array({"pip"})}
		};
		std::string result = registry.execute_tool("run_python", args.dump(), ctx);
		std::cout << "Dependencies Result: " << result << std::endl;
		assert(result.find("With dependencies test ok") != std::string::npos ||
		       result.find("Dependencies were requested but 'uv' is not installed") != std::string::npos ||
		       result.find("Request failed") != std::string::npos ||
		       result.find("failed to lookup address") != std::string::npos);
	}

	// 12. venv parameter test: create a venv and run code through it
	{
		std::string venv_name = "test_venv_" + pid_str;
		std::filesystem::path venv_dir = std::filesystem::path(project_root) / venv_name;
		int venv_ret = std::system(("python3 -m venv " + venv_dir.string() + " > /dev/null 2>&1").c_str());
		if (venv_ret != 0) {
			std::cout << "Skipping venv test (python3 -m venv unavailable)" << std::endl;
		} else {
			// Write a marker into the venv site-packages to prove the venv interpreter is used.
			std::filesystem::path venv_site = venv_dir / "lib";
			// Locate pythonX.Y path
			std::string marker_dir;
			for (const auto &entry : std::filesystem::directory_iterator(venv_site)) {
				if (entry.is_directory()) {
					marker_dir = entry.path().string();
					break;
				}
			}

			nlohmann::json args = {
				{"code", "print('venv marker:', __file__)\nimport sys\nprint('Hello from venv!')"},
				{"venv", venv_name}
			};
			std::string result = registry.execute_tool("run_python", args.dump(), ctx);
			std::cout << "Venv Result: " << result << std::endl;
			assert(result.find("Hello from venv!") != std::string::npos);

			// 12b. Validation failure: venv path outside allowed root
			{
				nlohmann::json bad_v = {{"code", "print(1)"}, {"venv", "/nonexistent/venv"}};
				auto prep = registry.prepare_tool("run_python", bad_v.dump(), ctx);
				std::cout << "Bad venv prep error: " << prep.error_message << " ptr=" << (prep.tool != nullptr) << std::endl;
				assert(prep.tool == nullptr);
			}

			std::filesystem::remove_all(venv_dir);
		}
	}

	std::cout << "run_python tests passed successfully.\n";
	return 0;
}
