#include "test_watchdog.h"
#include "agentlib/virtual_file_system.h"
#include "vfs/system_vfs_provider.h"
#include <cassert>
#include <iostream>

using namespace agentlib;

int main()
{
	test_watchdog::setup_watchdog(30);

	std::cout << "Testing system_vfs_provider..." << std::endl;

	virtual_file_system vfs;

	// 1. Test static document existence & read
	assert(vfs.exists("system://languages/cpp23.md"));
	assert(vfs.exists("system://languages/c17.md"));
	assert(vfs.exists("system://languages/python311.md"));
	assert(vfs.exists("system://languages/rust2021.md"));
	assert(vfs.exists("system://languages/typescript.md"));
	assert(vfs.exists("system://languages/verilog.md"));
	assert(vfs.exists("system://workflows/code_review.md"));
	assert(vfs.exists("system://workflows/crash_analysis.md"));

	auto cpp_doc = vfs.read_file("system://languages/cpp23.md");
	assert(cpp_doc.has_value());
	std::string cpp_text = std::string((*cpp_doc)->view());
	std::cout << "[system://languages/cpp23.md content snippet]:\n" << cpp_text.substr(0, 100) << "...\n" << std::endl;
	assert(cpp_text.find("Modern C++23 Coding Guidelines") != std::string::npos);

	// 2. Test fallback alias URIs
	assert(vfs.exists("system://cpp23.md"));
	assert(vfs.exists("system://c.md"));
	assert(vfs.exists("system://rust.md"));
	assert(vfs.exists("system://ts.md"));
	assert(vfs.exists("system://verilog.md"));
	auto alias_doc = vfs.read_file("system://cpp23.md");
	assert(alias_doc.has_value());
	assert(std::string((*alias_doc)->view()) == cpp_text);

	// 3. Test dynamic generators
	assert(vfs.exists("system://agents.md"));
	assert(vfs.exists("system://tools.md"));
	assert(vfs.exists("system://mcp.md"));

	auto agents_doc = vfs.read_file("system://agents.md");
	assert(agents_doc.has_value());
	std::string agents_text = std::string((*agents_doc)->view());
	std::cout << "[system://agents.md snippet]:\n" << agents_text.substr(0, 120) << "...\n" << std::endl;
	assert(agents_text.find("# Available Subagents") != std::string::npos);

	auto tools_doc = vfs.read_file("system://tools.md");
	assert(tools_doc.has_value());
	std::string tools_text = std::string((*tools_doc)->view());
	assert(tools_text.find("# Registered System Tools") != std::string::npos);

	// 4. Test directory listing
	auto root_list = vfs.list_directory("system://");
	assert(!root_list.empty());
	std::cout << "system:// directory listing count: " << root_list.size() << std::endl;
	for (const auto &item : root_list) {
		std::cout << "  - " << item.uri << std::endl;
	}

	auto lang_list = vfs.list_directory("system://languages/");
	assert(!lang_list.empty());
	assert(lang_list.size() >= 2);

	std::cout << "test_system_vfs passed successfully!" << std::endl;
	return 0;
}
