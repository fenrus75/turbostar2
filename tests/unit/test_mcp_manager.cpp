#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "../../src/config_manager.h"
#include "../../src/event_logger.h"
#include "../../src/mcp/mcp_manager.h"

namespace fs = std::filesystem;
using namespace agentlib;

void write_file(const fs::path &path, const std::string &content)
{
	fs::create_directories(path.parent_path());
	std::ofstream out(path);
	out << content;
}

int main()
{
	std::cout << "Testing MCP Manager..." << std::endl;

	// 1. Isolate HOME environment
	fs::path temp_home = fs::absolute("./test_mcp_home");
	fs::path temp_proj = fs::absolute("./test_mcp_project");

	if (fs::exists(temp_home)) {
		fs::remove_all(temp_home);
	}
	if (fs::exists(temp_proj)) {
		fs::remove_all(temp_proj);
	}

	fs::create_directories(temp_home);
	fs::create_directories(temp_proj);

	setenv("HOME", temp_home.c_str(), 1);

	// 2. Setup mock discovery files
	std::string claude_json = R"({
		"mcpServers": {
			"everything-system": {
				"command": "npx",
				"args": ["-y", "@modelcontextprotocol/server-everything"],
				"env": { "SYSTEM_VAR": "sys" }
			},
			"conflict-server": {
				"command": "uvx",
				"args": ["some-system-tool"]
			}
		}
	})";

	std::string gemini_json = R"({
		"mcpServers": {
			"gemini-system": {
				"command": "python3",
				"args": ["run_system.py"]
			}
		}
	})";

	std::string project_json = R"({
		"mcpServers": {
			"project-local": {
				"command": "npm",
				"args": ["run", "local"]
			},
			"conflict-server": {
				"command": "uvx",
				"args": ["local-args-override"]
			}
		}
	})";

	write_file(temp_home / ".claude/mcp.json", claude_json);
	write_file(temp_home / ".gemini/config/mcp_config.json", gemini_json);
	write_file(temp_proj / ".agents/mcp_config.json", project_json);

	// Initialize config_manager (it reads from isolated HOME)
	config_manager::get_instance().load();

	// 3. Trigger Discovery with override enabled project-local
	config_manager::get_instance().set_mcp_server_enabled("conflict-server", false, true);

	mcp_manager &manager = mcp_manager::get_instance();
	manager.discover_and_load(temp_proj.string());

	const auto &servers = manager.get_servers();

	// Assert discovered count (everything-system, gemini-system, project-local, conflict-server(project overrides))
	std::cout << "Servers found: " << servers.size() << std::endl;
	assert(servers.size() == 4);

	auto s_everything = manager.find_server("everything-system");
	assert(s_everything != nullptr);
	assert(s_everything->is_system() == true);
	assert(s_everything->get_mcp_type() == "npm");
	assert(s_everything->get_env().at("SYSTEM_VAR") == "sys");
	assert(s_everything->is_enabled() == true); // System is enabled by default

	auto s_gemini = manager.find_server("gemini-system");
	assert(s_gemini != nullptr);
	assert(s_gemini->is_system() == true);
	assert(s_gemini->get_mcp_type() == "python");

	auto s_local = manager.find_server("project-local");
	assert(s_local != nullptr);
	assert(s_local->is_system() == false);
	assert(s_local->is_enabled() == false); // Project is disabled by default

	// Verify project-local override of conflict-server
	auto s_conflict = manager.find_server("conflict-server");
	assert(s_conflict != nullptr);
	assert(s_conflict->is_system() == false); // overridden by project level because it was enabled project-local
	assert(s_conflict->get_args()[0] == "local-args-override");

	// 4. Test Twist in Conflict Resolution:
	// If the global one is enabled and the local one is disabled, the global one should win.
	// Reset state
	config_manager::get_instance().set_mcp_server_enabled("conflict-server", false, false); // disabled project-local
	config_manager::get_instance().set_mcp_server_enabled("conflict-server", true, true);	// enabled system-level
	manager.discover_and_load(temp_proj.string());

	// Re-fetch conflict server
	s_conflict = manager.find_server("conflict-server");
	assert(s_conflict != nullptr);
	assert(s_conflict->is_system() == true); // system wins because global is enabled and project is disabled
	assert(s_conflict->get_args()[0] == "some-system-tool");

	// 5. Test State Save & Reload via config_manager
	s_local = manager.find_server("project-local");
	assert(s_local != nullptr);
	s_local->set_enabled(true); // Enable project-local

	manager.save_configs(temp_proj.string());

	// Reload config manager
	config_manager::get_instance().load();
	assert(config_manager::get_instance().is_mcp_server_enabled("project-local", false) == true);

	// Cleanup files
	fs::remove_all(temp_home);
	fs::remove_all(temp_proj);

	std::cout << "All MCP Manager tests passed!" << std::endl;
	return 0;
}
