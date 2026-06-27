#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/project_manager.h"
#include "test_watchdog.h"

using namespace agentlib;

extern "C" void register_security_scan_semgrep(void);
extern "C" void unregister_security_scan_semgrep(void);

int main()
{
	test_watchdog::setup_watchdog(30);

	// Initialize the project manager
	project_manager::get_instance().initialize();

	// Ensure the security scan semgrep tool is registered
	register_security_scan_semgrep();

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;
	ctx.properties.active_families = {"base", ":plugin:securityagent"};

	std::string project_root = project_manager::get_instance().get_project_root();
	ctx.fs_security.set_working_directory(project_root);
	ctx.fs_security.add_allowed_root(project_root, access_type::read);
	ctx.fs_security.add_allowed_root(project_root, access_type::write);

	std::cout << "Testing security_scan_semgrep tool..." << std::endl;

	// Define lambda for checking family status
	bool sec_active = true;
	ctx.is_family_active = [&](const std::string &fam) {
		if (fam == ":plugin:securityagent") {
			return sec_active;
		}
		return true;
	};

	// 1. Verify family security restrictions (non-active family)
	sec_active = false;
	std::string valid_args = "{\"paths\": [\"src/document_undo.cpp\"]}";
	{
		auto prep = registry.prepare_tool("security_scan_semgrep", valid_args, ctx);
		assert(prep.tool == nullptr && "Should fail prepare when family is inactive");
		assert(prep.error_message.find("not active") != std::string::npos);
	}

	sec_active = true;

	// 2. Verify invalid parameters schema (empty paths list)
	{
		std::string args = "{\"paths\": []}";
		auto prep = registry.prepare_tool("security_scan_semgrep", args, ctx);
		assert(prep.tool == nullptr && "Should fail prepare when paths list is empty");
		assert(prep.error_message.find("empty") != std::string::npos);
	}

	// 3. Verify path validation (nonexistent file)
	{
		std::string args = "{\"paths\": [\"nonexistent_semgrep_file.cpp\"]}";
		auto prep = registry.prepare_tool("security_scan_semgrep", args, ctx);
		assert(prep.tool == nullptr && "Should fail prepare when file does not exist");
	}

	// 4. Verify path validation (directory traversal escape attempt)
	{
		std::string args = "{\"paths\": [\"../../etc/passwd\"]}";
		auto prep = registry.prepare_tool("security_scan_semgrep", args, ctx);
		assert(prep.tool == nullptr && "Should fail prepare on directory traversal attempt");
	}

	// 5. Test scanning a python file
	{
		std::string dummy_file = "ts_sec_dummy.py";
		std::string dummy_file_abs = project_root + "/" + dummy_file;
		std::ofstream out(dummy_file_abs);
		out << "import os\nprint('hello')\n";
		out.close();

		std::string args = "{\"paths\": [\"" + dummy_file + "\"]}";
		std::string res = registry.execute_tool("security_scan_semgrep", args, ctx);

		std::cout << "Semgrep output for dummy file:\n" << res << std::endl;

		// Clean up the dummy file
		std::filesystem::remove(dummy_file_abs);

		// The output should be a valid JSON containing 'results' or 'paths'
		assert(res.find("\"results\":") != std::string::npos);
		assert(res.find("\"paths\":") != std::string::npos);
	}

	// 6. Test scanning an HTML file
	{
		std::string dummy_file = "ts_sec_dummy.html";
		std::string dummy_file_abs = project_root + "/" + dummy_file;
		std::ofstream out(dummy_file_abs);
		out << "<!DOCTYPE html>\n<html>\n<head><title>Test</title></head>\n<body>\n<h1>Hello</h1>\n</body>\n</html>\n";
		out.close();

		std::string args = "{\"paths\": [\"" + dummy_file + "\"]}";
		std::string res = registry.execute_tool("security_scan_semgrep", args, ctx);

		std::cout << "Semgrep output for dummy HTML file:\n" << res << std::endl;

		// Clean up the dummy file
		std::filesystem::remove(dummy_file_abs);

		// The output should be a valid JSON containing 'results' or 'paths'
		assert(res.find("\"results\":") != std::string::npos);
		assert(res.find("\"paths\":") != std::string::npos);
	}

	// Clean up registration
	unregister_security_scan_semgrep();

	std::cout << "security_scan_semgrep tool verified successfully!" << std::endl;
	return 0;
}
