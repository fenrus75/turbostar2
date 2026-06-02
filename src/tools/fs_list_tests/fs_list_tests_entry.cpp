#include "../../project_manager.h"
#include "../../markdown_utils.h"
#include "fs_list_tests.h"
#include <sstream>

namespace tools
{

fs_list_tests_tool::fs_list_tests_tool(fs_list_tests_args args) : args_(std::move(args)) {}

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

	std::vector<std::string> filtered_tests;
	if (!args_.pattern.empty()) {
		re2::RE2::Options options;
		options.set_case_sensitive(false);
		re2::RE2 re(args_.pattern, options);
		
		for (const auto &t : tests) {
			if (re2::RE2::PartialMatch(t, re)) {
				filtered_tests.push_back(t);
			}
		}
	} else {
		filtered_tests = tests;
	}

	if (filtered_tests.empty()) {
		return "No tests matching the pattern '" + args_.pattern + "' were found.";
	}

	std::stringstream ss;
	ss << "Available Tests:\n\n";
	ss << "| Test Name |\n";
	ss << "| :--- |\n";
	for (const auto &t : filtered_tests) {
		ss << "| " << t << " |\n";
	}

	return ss.str();
}

} // namespace tools
