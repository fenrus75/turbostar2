#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include <nlohmann/json.hpp>
#include "../../src/agentlib/tool_registry.h"
#include "../../src/perf_manager.h"
#include "../../src/project_manager.h"

using namespace agentlib;
using namespace turbostar;

int main()
{
	test_watchdog::setup_watchdog(30);
	project_manager::get_instance().initialize();

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;

	ctx.fs_security.set_working_directory(project_manager::get_instance().get_project_root());
	ctx.fs_security.add_allowed_root(project_manager::get_instance().get_project_root(), access_type::read);
	ctx.fs_security.add_allowed_root(project_manager::get_instance().get_project_root(), access_type::write);

	std::cout << "Testing agent_get_profile_summary and agent_get_profile_details..." << std::endl;

	// Populate synthetic profile report in perf_manager
	perf_profile_report report;
	report.total_samples = 1000;
	report.top_functions.push_back(
		perf_function_sample{.function_name = "test_func_a", .file_path = "src/main.cpp", .line_number = 42, .count = 600, .percentage = 60.0});
	report.top_functions.push_back(
		perf_function_sample{.function_name = "test_func_b", .file_path = "src/utils.cpp", .line_number = 15, .count = 400, .percentage = 40.0});
	report.top_lines.push_back(perf_line_sample{.file_path = "src/main.cpp", .line_number = 42, .function_name = "test_func_a", .count = 600, .percentage = 60.0});
	report.top_lines.push_back(perf_line_sample{.file_path = "src/utils.cpp", .line_number = 15, .function_name = "test_func_b", .count = 400, .percentage = 40.0});
	report.line_samples_by_file["src/main.cpp"].push_back(
		perf_line_sample{.file_path = "src/main.cpp", .line_number = 42, .function_name = "test_func_a", .count = 600, .percentage = 60.0});

	perf_manager::get_instance().set_active_profile(report);

	// 1. Test agent_get_profile_summary
	{
		auto prep = registry.prepare_tool("agent_get_profile_summary", "{\"limit\": 5}", ctx);
		assert(prep.tool != nullptr);
		assert(prep.error_message.empty());

		std::string result = prep.tool->execute(ctx);
		auto res_json = nlohmann::json::parse(result);
		assert(res_json["total_samples"] == 1000);
		assert(res_json["top_functions"].size() == 2);
		assert(res_json["top_functions"][0]["function_name"] == "test_func_a");
		std::cout << "agent_get_profile_summary verified successfully!" << std::endl;
	}

	// 2. Test agent_get_profile_details by file
	{
		auto prep = registry.prepare_tool("agent_get_profile_details", "{\"file_path\": \"src/main.cpp\"}", ctx);
		assert(prep.tool != nullptr);
		assert(prep.error_message.empty());

		std::string result = prep.tool->execute(ctx);
		auto res_json = nlohmann::json::parse(result);
		assert(res_json["total_samples"] == 1000);
		assert(res_json["target_samples"] == 600);
		assert(!res_json["line_samples"].empty());
		bool found_line_42 = false;
		for (const auto &ls : res_json["line_samples"]) {
			if (ls["line_number"] == 42) {
				found_line_42 = true;
				assert(ls["count"] == 600);
				assert(ls["global_percentage"] == 60.0);
				assert(ls["file_percentage"] == 100.0);
			}
		}
		assert(found_line_42);
		std::cout << "agent_get_profile_details (file filter) verified successfully!" << std::endl;
	}

	// 3. Test agent_get_profile_details by function name (substring match)
	{
		auto prep = registry.prepare_tool("agent_get_profile_details", "{\"function_name\": \"test_func_a\"}", ctx);
		assert(prep.tool != nullptr);
		assert(prep.error_message.empty());

		std::string result = prep.tool->execute(ctx);
		auto res_json = nlohmann::json::parse(result);
		assert(res_json["total_samples"] == 1000);
		assert(res_json["target_samples"] == 600);
		assert(!res_json["line_samples"].empty());
		std::cout << "agent_get_profile_details (function_name filter) verified successfully!" << std::endl;
	}

	// 3b. Test agent_get_profile_details by mismatched signature function name (e.g. test_func_c() matching test_func_c(int, double))
	{
		report.top_functions.push_back(
			perf_function_sample{.function_name = "test_func_c(int, double)", .file_path = "src/math.cpp", .line_number = 88, .count = 200, .percentage = 20.0});
		report.top_lines.push_back(
			perf_line_sample{.file_path = "src/math.cpp", .line_number = 88, .function_name = "test_func_c(int, double)", .count = 200, .percentage = 20.0});
		perf_manager::get_instance().set_active_profile(report);

		auto prep = registry.prepare_tool("agent_get_profile_details", "{\"function_name\": \"test_func_c()\"}", ctx);
		assert(prep.tool != nullptr);
		assert(prep.error_message.empty());

		std::string result = prep.tool->execute(ctx);
		auto res_json = nlohmann::json::parse(result);
		assert(res_json["target_samples"] == 200);
		assert(!res_json["line_samples"].empty());
		std::cout << "agent_get_profile_details (mismatched signature filter) verified successfully!" << std::endl;
	}

	// 4. Test agent_get_profile_details all
	{
		auto prep = registry.prepare_tool("agent_get_profile_details", "{}", ctx);
		assert(prep.tool != nullptr);
		assert(prep.error_message.empty());

		std::string result = prep.tool->execute(ctx);
		auto res_json = nlohmann::json::parse(result);
		assert(!res_json["line_samples"].empty());
		std::cout << "agent_get_profile_details (all) verified successfully!" << std::endl;
	}

	// 5. Test agent_get_profile_summary and details with run_id parameter
	{
		perf_profile_report run_a;
		run_a.total_samples = 2000;
		run_a.top_functions.push_back(perf_function_sample{.function_name = "func_run_a", .file_path = "src/a.cpp", .count = 2000, .percentage = 100.0});

		perf_profile_report run_b;
		run_b.total_samples = 3000;
		run_b.top_functions.push_back(perf_function_sample{.function_name = "func_run_b", .file_path = "src/b.cpp", .count = 3000, .percentage = 100.0});

		perf_manager::get_instance().set_active_profile(run_a, "run_1");
		perf_manager::get_instance().set_active_profile(run_b, "run_2");

		// Test summary for run_1 via string "run_1" and integer 1
		auto prep1 = registry.prepare_tool("agent_get_profile_summary", "{\"run_id\": \"run_1\"}", ctx);
		assert(prep1.tool != nullptr);
		auto res1 = nlohmann::json::parse(prep1.tool->execute(ctx));
		assert(res1["total_samples"] == 2000);
		assert(res1["run_id"] == "run_1");

		auto prep1_num = registry.prepare_tool("agent_get_profile_summary", "{\"run_id\": 1}", ctx);
		assert(prep1_num.tool != nullptr);
		auto res1_num = nlohmann::json::parse(prep1_num.tool->execute(ctx));
		assert(res1_num["total_samples"] == 2000);

		// Test summary for run_2
		auto prep2 = registry.prepare_tool("agent_get_profile_summary", "{\"run_id\": \"run_2\"}", ctx);
		assert(prep2.tool != nullptr);
		auto res2 = nlohmann::json::parse(prep2.tool->execute(ctx));
		assert(res2["total_samples"] == 3000);
		assert(res2["run_id"] == "run_2");

		std::cout << "agent_get_profile_summary and details with run_id parameter verified successfully!" << std::endl;
	}

	perf_manager::get_instance().clear_active_profile();
	std::cout << "Profile tools tests passed successfully!" << std::endl;
	return 0;
}
