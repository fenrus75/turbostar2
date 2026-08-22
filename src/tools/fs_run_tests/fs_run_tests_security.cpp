#include "agentlib/tool_registry.h"
#include "fs_utils.h"
#include "fs_run_tests.h"
#include <algorithm>

namespace tools
{

bool fs_run_tests_validator::validate_args_impl(const nlohmann::json &args, const agentlib::tool_context & /*ctx*/, std::string &out_error) const
{
	int timeout = 300;
	if (args.contains("timeout")) {
		if (!args["timeout"].is_number_integer()) {
			out_error = "Invalid 'timeout' parameter: must be an integer.";
			return false;
		}
		timeout = std::clamp<int>(args["timeout"].get<int>(), 1, 3600);
	}

	std::vector<std::string> test_names;
	if (args.contains("test_names")) {
		if (!args["test_names"].is_array()) {
			out_error = "Invalid 'test_names' parameter: must be an array of strings.";
			return false;
		}
		if (args["test_names"].size() > 100) {
			out_error = "Validation Error: 'test_names' array exceeds maximum limit of 100 entries.";
			return false;
		}
		for (const auto &item : args["test_names"]) {
			if (!item.is_string()) {
				out_error = "Invalid 'test_names' entry: must be a string.";
				return false;
			}
			std::string name = item.get<std::string>();
			if (!fs_utils::is_safe_for_ui(name)) {
				out_error = "Security Violation: test name contains unsafe control characters.";
				return false;
			}
			test_names.push_back(name);
		}
	}

	parsed_test_names_ = test_names;
	parsed_timeout_ = timeout;
	return true;
}

std::unique_ptr<agentlib::llm_tool> fs_run_tests_validator::create_tool_impl(const nlohmann::json & /*args*/) const
{
	return std::make_unique<fs_run_tests_tool>(parsed_test_names_, parsed_timeout_);
}

std::vector<agentlib::tool_example> fs_run_tests_validator::get_examples() const
{
	return {
		{
			"Targeted Test Execution by Exact Name",
			nlohmann::json{{"test_names", nlohmann::json::array({"unit_fs_grep_files"})}},
			"Full Flow: 1) Call fs_read_lines(path='system://project/testlist.md?search=fs_grep') to discover exact test target names -> 2) Call fs_run_tests(test_names=['unit_fs_grep_files']) to execute."
		},
		{
			"Run All Project Tests with Timeout",
			nlohmann::json{{"timeout", 60}},
			"Runs full project test suite with a 60-second timeout."
		}
	};
}



REGISTER_TOOL(fs_run_tests_validator)
} // namespace tools