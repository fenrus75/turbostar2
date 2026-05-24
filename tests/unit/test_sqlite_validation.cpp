#include <cassert>
#include <iostream>
#include <vector>
#include "../../src/agentlib/skill_manager.h"
#include "../../src/agentlib/tool_context.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/fs_utils.h"

void test_is_valid_db_name()
{
	// Valid names
	assert(fs_utils::is_valid_db_name("testdb"));
	assert(fs_utils::is_valid_db_name("db123"));
	assert(fs_utils::is_valid_db_name("my_db-name"));

	// Invalid names
	assert(!fs_utils::is_valid_db_name(""));
	assert(!fs_utils::is_valid_db_name("test.db"));
	assert(!fs_utils::is_valid_db_name("test db"));
	assert(!fs_utils::is_valid_db_name("../testdb"));
	assert(!fs_utils::is_valid_db_name("testdb; DROP TABLE"));
	assert(!fs_utils::is_valid_db_name("testdb/"));
	assert(!fs_utils::is_valid_db_name("/testdb"));
	assert(!fs_utils::is_valid_db_name("testdb\\"));
	assert(!fs_utils::is_valid_db_name("testdb'"));
	assert(!fs_utils::is_valid_db_name("testdb\""));
}

void test_tool_object_creation()
{
	agentlib::skill_manager::get_instance().initialize();
	agentlib::tool_context ctx;
	auto &registry = agentlib::tool_registry::get_instance();

	std::vector<std::string> bad_names = {
	    "", "test.db", "test db", "../testdb", "testdb; DROP TABLE", "/etc/passwd", "\\windows\\system32", "my'db", "my\"db"};

	for (const auto &bad_name : bad_names) {
		// Try sqlite_create_db
		std::string json_arg = "{\"database\": \"" + bad_name + "\"}";
		std::string res = registry.execute_tool("sqlite_create_db", json_arg, ctx);
		// We expect it to fail at stage 1 or parsing
		assert(res.find("Error parsing") == 0 || res.find("Stage 1 Security Violation:") == 0);

		// Try sqlite_delete_db
		res = registry.execute_tool("sqlite_delete_db", json_arg, ctx);
		assert(res.find("Error parsing") == 0 || res.find("Stage 1 Security Violation:") == 0);

		// Try sqlite_perform
		std::string json_arg_perf = "{\"database\": \"" + bad_name + "\", \"query\": \"SELECT 1;\"}";
		res = registry.execute_tool("sqlite_perform", json_arg_perf, ctx);
		assert(res.find("Error parsing") == 0 || res.find("Stage 1 Security Violation:") == 0);
	}
}

int main()
{
	test_is_valid_db_name();
	test_tool_object_creation();
	std::cout << "SQLite validation unit tests passed successfully!\n";
	return 0;
}