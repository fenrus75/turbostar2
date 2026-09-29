#include <algorithm>
#include <filesystem>
#include <format>
#include "agentlib/tool_registry.h"
#include "config_manager.h"
#include "fs_run_tests.h"
#include "fs_utils.h"
#include "project_manager.h"

namespace tools
{

bool fs_run_tests_validator::validate_args_impl(const nlohmann::json &args, const agentlib::tool_context &ctx,
						std::string &out_error) const
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
		if (args["test_names"].is_string()) {
			std::string name = args["test_names"].get<std::string>();
			if (!fs_utils::is_safe_for_ui(name)) {
				out_error = "Security Violation: test name contains unsafe control characters.";
				return false;
			}
			if (!name.empty()) {
				test_names.push_back(name);
			}
		} else if (args["test_names"].is_array()) {
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
		} else {
			out_error = "Invalid 'test_names' parameter: must be an array of strings or a single string.";
			return false;
		}
	}

	std::string safe_verbose = "auto";
	if (args.contains("verbose")) {
		const auto &untrusted_verbose = args["verbose"];
		if (untrusted_verbose.is_boolean()) {
			safe_verbose = untrusted_verbose.get<bool>() ? "true" : "false";
		} else if (untrusted_verbose.is_string()) {
			std::string untrusted_val = untrusted_verbose.get<std::string>();
			std::string val = untrusted_val;
			std::transform(val.begin(), val.end(), val.begin(),
				       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (val == "auto" || val == "true" || val == "false") {
				safe_verbose = val;
			} else {
				out_error = "Invalid 'verbose' parameter: must be 'auto', 'true', or 'false' (or boolean true/false).";
				return false;
			}
		} else {
			out_error = "Invalid 'verbose' parameter: must be a string ('auto', 'true', 'false') or a boolean.";
			return false;
		}
	}

	std::string untrusted_build_dir;
	if (args.contains("build_dir")) {
		if (!args["build_dir"].is_string()) {
			out_error = "Invalid 'build_dir' parameter: must be a string.";
			return false;
		}
		untrusted_build_dir = args["build_dir"].get<std::string>();
	}

	std::string safe_build_dir;
	if (!untrusted_build_dir.empty()) {
		if (!ctx.fs_security.validate_access(untrusted_build_dir, agentlib::access_type::read, safe_build_dir, out_error)) {
			out_error = "Access denied for build_dir '" + untrusted_build_dir + "': " + out_error;
			return false;
		}
		std::error_code ec;
		if (!std::filesystem::is_directory(safe_build_dir, ec)) {
			out_error = std::format("Error: build_dir '{}' does not exist or is not a directory.", untrusted_build_dir);
			return false;
		}
		std::string build_system = config_manager::get_instance().get_build_system();
		if (!project_manager::is_build_directory_valid(safe_build_dir, build_system)) {
			out_error =
			    std::format("Error: build_dir '{}' is not a configured {} build directory (missing build definition files).",
					untrusted_build_dir, build_system);
			return false;
		}
	}

	parsed_test_names_ = test_names;
	parsed_timeout_ = timeout;
	parsed_verbose_ = safe_verbose;
	parsed_build_dir_ = safe_build_dir;
	return true;
}

std::unique_ptr<agentlib::llm_tool> fs_run_tests_validator::create_tool_impl(const nlohmann::json & /*args*/) const
{
	return std::make_unique<fs_run_tests_tool>(parsed_test_names_, parsed_timeout_, parsed_verbose_, parsed_build_dir_);
}

std::vector<agentlib::tool_example> fs_run_tests_validator::get_examples() const
{
	return {{"Targeted Test Execution by Exact Name", nlohmann::json{{"test_names", nlohmann::json::array({"unit_fs_grep_files"})}},
		 "Full Flow: 1) Call fs_read_lines(path='system://project/testlist.md?search=fs_grep') to discover exact test target names "
		 "-> 2) Call fs_run_tests(test_names=['unit_fs_grep_files']) to execute."},
		{"Run Single Test with Full Verbose Output",
		 nlohmann::json{{"test_names", nlohmann::json::array({"unit_event_logger"})}, {"verbose", true}},
		 "Runs a single test and displays full stdout and stderr output regardless of pass or fail."},
		{"Run All Project Tests with Timeout", nlohmann::json{{"timeout", 60}},
		 "Runs full project test suite with a 60-second timeout."}};
}

REGISTER_TOOL(fs_run_tests_validator)
} // namespace tools