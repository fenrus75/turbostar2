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

REGISTER_TOOL(fs_run_tests_validator)
}