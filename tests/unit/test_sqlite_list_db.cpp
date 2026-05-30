#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/project_manager.h"
#include "../../src/fs_utils.h"

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

	std::string db_dir = fs_utils::get_project_db_dir();
	std::filesystem::path db1_path = std::filesystem::path(db_dir) / "test_sqlite_list_db_1.db";
	std::filesystem::path db2_path = std::filesystem::path(db_dir) / "test_sqlite_list_db_2.db";

	write_file(db1_path, "dummy sql content 1");
	write_file(db2_path, "dummy sql content 2");

	std::cout << "Testing sqlite_list_db..." << std::endl;

	// 1. Success case: execute sqlite_list_db
	{
		std::string result = registry.execute_tool("sqlite_list_db", "{}", ctx);
		std::cout << "Result: " << result << std::endl;
		assert(!result.empty());
		assert(result.find("test_sqlite_list_db_1") != std::string::npos);
		assert(result.find("test_sqlite_list_db_2") != std::string::npos);
	}

	// 2. Validation failure: unexpected arguments (should fail validation as per review recommendations)
	{
		nlohmann::json args = {{"unexpected_arg", 123}};
		auto prep = registry.prepare_tool("sqlite_list_db", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	// Clean up
	std::filesystem::remove(db1_path);
	std::filesystem::remove(db2_path);

	std::cout << "sqlite_list_db tests passed successfully.\n";
	return 0;
}
