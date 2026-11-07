// Tested source file: src/tools/agent_get_profile_details/agent_get_profile_details_entry.cpp
#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include <nlohmann/json.hpp>
#include "agentlib/tool_registry.h"
#include "perf_manager.h"
#include "project_manager.h"
#include "fs_utils.h"

using namespace agentlib;
using namespace turbostar;

static nlohmann::json parse_tool_json(std::string result)
{
	return nlohmann::json::parse(fs_utils::unwrap_prompt_untrusted_data_tag(result));
}

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
		auto prep = registry.prepare_tool("agent_get_profile_summary", R"({"limit": 5, "format": "json"})", ctx);
		assert(prep.tool != nullptr);
		assert(prep.error_message.empty());

		std::string result = prep.tool->execute(ctx);
		auto res_json = nlohmann::json::parse(fs_utils::unwrap_prompt_untrusted_data_tag(result));
		assert(res_json["total_samples"] == 1000);
		assert(res_json["top_functions"].size() == 2);
		assert(res_json["top_functions"][0]["function_name"] == "test_func_a");
		std::cout << "agent_get_profile_summary verified successfully!" << std::endl;
	}

	// 2. Test agent_get_profile_details by file
	{
		auto prep = registry.prepare_tool("agent_get_profile_details", R"({"path": "src/main.cpp", "format": "json"})", ctx);
		assert(prep.tool != nullptr);
		assert(prep.error_message.empty());

		std::string result = prep.tool->execute(ctx);
		if (result.starts_with("<agent_profile_details_result>")) {
			result = result.substr(std::string("<agent_profile_details_result>").length());
			size_t end_pos = result.rfind("</agent_profile_details_result>");
			if (end_pos != std::string::npos) {
				result = result.substr(0, end_pos);
			}
		}
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
		auto prep = registry.prepare_tool("agent_get_profile_details", R"({"function_name": "test_func_a", "format": "json"})", ctx);
		assert(prep.tool != nullptr);
		assert(prep.error_message.empty());

		std::string result = prep.tool->execute(ctx);
		if (result.starts_with("<agent_profile_details_result>")) {
			result = result.substr(std::string("<agent_profile_details_result>").length());
			size_t end_pos = result.rfind("</agent_profile_details_result>");
			if (end_pos != std::string::npos) {
				result = result.substr(0, end_pos);
			}
		}
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

		auto prep = registry.prepare_tool("agent_get_profile_details", "{\"function_name\": \"test_func_c()\", \"format\": \"json\"}", ctx);
		assert(prep.tool != nullptr);
		assert(prep.error_message.empty());

		std::string result = prep.tool->execute(ctx);
		auto res_json = parse_tool_json(result);
		assert(res_json["target_samples"] == 200);
		assert(!res_json["line_samples"].empty());
		std::cout << "agent_get_profile_details (mismatched signature filter) verified successfully!" << std::endl;
	}

	// 4. Test agent_get_profile_details all
	{
		auto prep = registry.prepare_tool("agent_get_profile_details", R"({"format": "json"})", ctx);
		assert(prep.tool != nullptr);
		assert(prep.error_message.empty());

		std::string result = prep.tool->execute(ctx);
		auto res_json = parse_tool_json(result);
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
		auto prep1 = registry.prepare_tool("agent_get_profile_summary", R"({"run_id": "run_1", "format": "json"})", ctx);
		assert(prep1.tool != nullptr);
		auto res1 = parse_tool_json(prep1.tool->execute(ctx));
		assert(res1["total_samples"] == 2000);
		assert(res1["run_id"] == "run_1");

		auto prep1_num = registry.prepare_tool("agent_get_profile_summary", R"({"run_id": 1, "format": "json"})", ctx);
		assert(prep1_num.tool != nullptr);
		auto res1_num = parse_tool_json(prep1_num.tool->execute(ctx));
		assert(res1_num["total_samples"] == 2000);

		// Test summary for run_2
		auto prep2 = registry.prepare_tool("agent_get_profile_summary", R"({"run_id": "run_2", "format": "json"})", ctx);
		assert(prep2.tool != nullptr);
		auto res2 = parse_tool_json(prep2.tool->execute(ctx));
		assert(res2["total_samples"] == 3000);
		assert(res2["run_id"] == "run_2");

		std::cout << "agent_get_profile_summary and details with run_id parameter verified successfully!" << std::endl;
	}

	// 6. Test agent_get_profile_details with relative/absolute path cross-resolution
	{
		perf_profile_report run_c;
		run_c.total_samples = 5000;
		run_c.top_functions.push_back(perf_function_sample{.function_name = "editor::dispatch(event const&)", .file_path = "src/editor.cpp", .line_number = 150, .count = 5000, .percentage = 100.0});
		run_c.line_samples_by_file[project_manager::get_instance().get_project_root() + "/src/editor.cpp"].push_back(
			perf_line_sample{.file_path = project_manager::get_instance().get_project_root() + "/src/editor.cpp", .line_number = 150, .function_name = "editor::dispatch", .count = 5000, .percentage = 100.0});

		perf_manager::get_instance().set_active_profile(run_c, "run_3");

		auto prep = registry.prepare_tool("agent_get_profile_details", R"({"run_id": "run_3", "function_name": "editor::dispatch", "format": "json"})", ctx);
		assert(prep.tool != nullptr);
		auto res = parse_tool_json(prep.tool->execute(ctx));
		assert(res["total_samples"] == 5000);
		assert(res["target_samples"] == 5000);
		assert(!res["line_samples"].empty());
		std::cout << "agent_get_profile_details (path cross-resolution) verified successfully!" << std::endl;
	}

	// 7. Test agent_get_profile_details multi-function file scope filtering
	{
		perf_profile_report run_d;
		run_d.total_samples = 5000;
		run_d.top_functions.push_back(perf_function_sample{.function_name = "is_prime_vA(int)", .file_path = "prime.cpp", .line_number = 6, .count = 3200, .percentage = 64.0});
		run_d.top_functions.push_back(perf_function_sample{.function_name = "is_prime_vB(int)", .file_path = "prime.cpp", .line_number = 16, .count = 1800, .percentage = 36.0});

		run_d.line_samples_by_file["prime.cpp"].push_back(
			perf_line_sample{.file_path = "prime.cpp", .line_number = 6, .function_name = "is_prime_vA(int)", .count = 3200, .percentage = 64.0});
		run_d.line_samples_by_file["prime.cpp"].push_back(
			perf_line_sample{.file_path = "prime.cpp", .line_number = 16, .function_name = "is_prime_vB(int)", .count = 1800, .percentage = 36.0});

		perf_manager::get_instance().set_active_profile(run_d, "run_4");

		auto prep = registry.prepare_tool("agent_get_profile_details", R"({"run_id": "run_4", "function_name": "is_prime_vA", "format": "json"})", ctx);
		assert(prep.tool != nullptr);
		auto res = parse_tool_json(prep.tool->execute(ctx));
		assert(res["total_samples"] == 5000);
		assert(res["target_samples"] == 3200);
		for (const auto &ls : res["line_samples"]) {
			if (ls["line_number"] == 16) {
				assert(false && "line 16 (is_prime_vB) must not be returned when querying is_prime_vA!");
			}
		}
		std::cout << "agent_get_profile_details (multi-function file scope filtering) verified successfully!" << std::endl;
	}

	// 8. Test agent_get_profile_details fallback when top_lines has virtual/external paths and target is only in line_samples_by_file
	{
		perf_profile_report run_e;
		run_e.total_samples = 10000;
		run_e.top_lines.push_back(
			perf_line_sample{.file_path = "include://bits/basic_string.h", .line_number = 235, .function_name = "std::string::_M_data", .count = 4000, .percentage = 40.0});
		run_e.top_lines.push_back(
			perf_line_sample{.file_path = "/build/reproducible-path/glibc-2.43/sysdeps/x86_64/multiarch/memcmp-avx2-movbe.S", .line_number = 415, .function_name = "__memcmp_avx2_movbe", .count = 3000, .percentage = 30.0});

		for (int i = 0; i < 50; ++i) {
			std::string ext_file = "include://c++/14/bits/header_" + std::to_string(i) + ".h";
			run_e.line_samples_by_file[ext_file].push_back(
				perf_line_sample{.file_path = ext_file, .line_number = 10, .function_name = "std::helper", .count = 10, .percentage = 0.1});
		}

		std::string abs_lexer = project_manager::get_instance().get_project_root() + "/src/languages/cpp/lexer.cpp";
		run_e.line_samples_by_file[abs_lexer].push_back(
			perf_line_sample{.file_path = abs_lexer, .line_number = 25, .function_name = "cpp_lexer::tokenize", .count = 250, .percentage = 2.5});

		perf_manager::get_instance().set_active_profile(run_e, "run_5");

		auto prep = registry.prepare_tool("agent_get_profile_details", R"({"run_id": "run_5", "file_path": "src/languages/cpp/lexer.cpp", "format": "json"})", ctx);
		assert(prep.tool != nullptr);
		auto res = parse_tool_json(prep.tool->execute(ctx));
		assert(res["total_samples"] == 10000);
		assert(res["target_samples"] == 250);
		assert(!res["line_samples"].empty());
		bool found_line_25 = false;
		for (const auto &ls : res["line_samples"]) {
			if (ls["line_number"] == 25 && ls["count"] == 250) {
				found_line_25 = true;
			}
		}
		assert(found_line_25);
		std::cout << "agent_get_profile_details (external URI/system path fast-skip + line_samples_by_file fallback) verified successfully!" << std::endl;
	}

	perf_manager::get_instance().clear_active_profile();
	std::cout << "Profile tools tests passed successfully!" << std::endl;
	return 0;
}
