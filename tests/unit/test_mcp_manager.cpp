#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "../../src/agentlib/tool_registry.h"
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

	// Write the Python mock MCP server script
	std::string python_mock = R"(import sys
import json

def main():
    while True:
        try:
            line = sys.stdin.readline()
            if not line:
                break
            req = json.loads(line)
            if "method" in req:
                method = req["method"]
                req_id = req.get("id")
                if method == "initialize":
                    resp = {
                        "jsonrpc": "2.0",
                        "id": req_id,
                        "result": {
                            "protocolVersion": "2024-11-05",
                            "capabilities": {},
                            "serverInfo": {"name": "mock-server", "version": "1.0.0"}
                        }
                    }
                    sys.stdout.write(json.dumps(resp) + "\n")
                    sys.stdout.flush()
                elif method == "tools/list":
                    resp = {
                        "jsonrpc": "2.0",
                        "id": req_id,
                        "result": {
                            "tools": [
                                {
                                    "name": "mock_tool",
                                    "description": "A mock test tool",
                                    "inputSchema": {
                                        "type": "object",
                                        "properties": {
                                            "msg": {"type": "string"}
                                        },
                                        "required": ["msg"]
                                    }
                                }
                            ]
                        }
                    }
                    sys.stdout.write(json.dumps(resp) + "\n")
                    sys.stdout.flush()
                elif method == "tools/call":
                    tool_name = req["params"]["name"]
                    args = req["params"]["arguments"]
                    msg_val = args.get("msg", "default")
                    
                    resp = {
                        "jsonrpc": "2.0",
                        "id": req_id,
                        "result": {
                            "content": [
                                {
                                    "type": "text",
                                    "text": "Called " + tool_name + " with msg: " + msg_val
                                }
                            ]
                        }
                    }
                    sys.stdout.write(json.dumps(resp) + "\n")
                    sys.stdout.flush()
        except Exception as e:
            sys.stderr.write(f"Error: {e}\n")
            sys.stderr.flush()

if __name__ == "__main__":
    main()
)";

	write_file(temp_proj / "mock_mcp.py", python_mock);

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
			},
			"mock-python-server": {
				"command": "python3",
				"args": ["-u", ")" +
				   (temp_proj / "mock_mcp.py").string() + R"("]
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

	// Assert discovered count (everything-system, gemini-system, project-local, conflict-server(project overrides), mock-python-server)
	std::cout << "Servers found: " << servers.size() << std::endl;
	assert(servers.size() == 5);

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

	// 6. Test Active Server Spawning & JSON-RPC Handshake / Tool Execution
	std::cout << "Testing server start and execution..." << std::endl;
	auto s_mock = manager.find_server("mock-python-server");
	assert(s_mock != nullptr);
	assert(s_mock->is_enabled() == false); // Project servers disabled by default
	s_mock->set_enabled(true);
	s_mock->set_system(true); // Bypass sandboxing for unit tests

	// Start enabled servers
	if (auto s = manager.find_server("everything-system"))
		s->set_enabled(false);
	if (auto s = manager.find_server("gemini-system"))
		s->set_enabled(false);
	if (auto s = manager.find_server("project-local"))
		s->set_enabled(false);
	if (auto s = manager.find_server("conflict-server"))
		s->set_enabled(false);

	manager.start_active_servers();
	assert(s_mock->is_running() == true);
	assert(s_mock->get_tools().size() == 1);
	assert(s_mock->get_tools()[0].name == "mock_tool");

	// Schema serialization check (get_tools_json should map colons to __)
	nlohmann::json tools_json = tool_registry::get_instance().get_tools_json();
	std::cout << "Discovered tools count: " << tools_json.size() << std::endl;
	bool found_serialized_tool = false;
	for (const auto &t : tools_json) {
		if (t.contains("function") && t["function"].contains("name") &&
		    t["function"]["name"].get<std::string>() == "mcp__mock-python-server__mock_tool") {
			found_serialized_tool = true;
			break;
		}
	}
	assert(found_serialized_tool == true);

	// Tool execution check
	tool_context ctx;
	std::string result =
	    tool_registry::get_instance().execute_tool("mcp:mock-python-server:mock_tool", "{\"msg\": \"antigravity\"}", ctx);
	std::cout << "Tool execution output: " << result << std::endl;
	assert(result == "Called mock_tool with msg: antigravity");

	// Stop servers
	manager.stop_all_servers();
	assert(s_mock->is_running() == false);

	// Cleanup files
	fs::remove_all(temp_home);
	fs::remove_all(temp_proj);

	std::cout << "All MCP Manager tests passed!" << std::endl;
	return 0;
}
