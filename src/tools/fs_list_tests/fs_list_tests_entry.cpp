#include "../../project_manager.h"
#include "../../markdown_utils.h"
#include "fs_list_tests.h"
#include <sstream>

namespace tools
{

bool fs_list_tests_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string fs_list_tests_tool::execute(agentlib::tool_context & /*ctx*/)
{
	auto tests = project_manager::get_instance().get_available_tests();

	if (tests.empty()) {
		return "No tests found or build system does not support test listing.";
	}

	std::stringstream ss;
	ss << "Available Tests:\n\n";
	ss << "| Test Name |\n";
	ss << "| :--- |\n";
	for (const auto &t : tests) {
		ss << "| " << t << " |\n";
	}

	return ss.str();
}

} // namespace tools
