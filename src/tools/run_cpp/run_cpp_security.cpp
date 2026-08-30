#include "run_cpp.h"
#include "agentlib/file_security_manager.h"
#include "agentlib/tool_registry.h"
#include <set>



namespace tools
{

nlohmann::json run_cpp_validator::get_parameters_schema() const
{
	return {
		{"type", "object"},
		{"properties", {
			{"code", {{"type", "string"}, {"description", "Inline C/C++ source code to compile and run. Omitting main() relies on automatic entry point wrapping, which is for trivial cases only; for non-trivial code (headers, functions, structs), provide complete source including main()."}}},
			{"path", {{"type", "string"}, {"description", "Optional relative workspace path or VFS URI to a .cpp or .c source file (e.g. 'src/main.cpp', 'tmp://probe.c'). Fully resolves VFS URIs (tmp://, include://, skills://)."}}},
			{"std", {{"type", "string"}, {"enum", nlohmann::json::array({"c++23", "c++20", "c++17", "c++14", "c++11", "c17", "c11", "c99", "c89", "gnu17", "gnu11"})}, {"default", "c++23"}, {"description", "C/C++ language standard to compile against. Defaults to 'c++23' for C++ sources/code, and auto-detects 'c17' for .c files."}}},
			{"includes", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Optional array of include directory paths, appended via -I (e.g. ['src/', 'tmp://', '/usr/include/']). Supports relative paths and VFS URIs. Note: -I flags are evaluated in array order."}}},
			{"defines", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Optional array of preprocessor macro definitions, appended via -D (e.g. ['DEBUG=1', 'USE_SSL']). Accepts 'NAME' or 'NAME=value' strings; the '-D' prefix is optional."}}},
			{"libraries", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Optional array of linker library flags (e.g. ['-lsqlite3', '-lssl']) or extra translation unit source files (e.g. ['src/fs_utils.cpp']) appended to the compiler command line."}}},
			{"timeout", {{"type", "integer"}, {"default", 10}, {"description", "Maximum execution runtime in seconds (1 to 60). Defaults to 10."}}}
		}}
	};
}

std::vector<agentlib::tool_example> run_cpp_validator::get_examples() const
{
	return {
		{
			"Inline C++23 Modern STL Probe",
			nlohmann::json{{"code", "std::cout << \"Sum: \" << (20 + 22) << std::endl;"}, {"std", "c++23"}},
			"Compiles and executes an inline C++23 snippet."
		},
		{
			"C11 API Probe with Library Flags",
			nlohmann::json{{"code", "printf(\"SQLite: %s\\n\", sqlite3_libversion());"}, {"std", "c11"}, {"libraries", nlohmann::json::array({"-lsqlite3"})}},
			"Compiles and executes a C11 snippet linked against libsqlite3."
		},
		{
			"VFS Source Probe with Macro Defines & Extra Files",
			nlohmann::json{{"path", "tmp://probe.c"}, {"defines", nlohmann::json::array({"DEBUG=1", "-DVERBOSE"})}, {"includes", nlohmann::json::array({"src/"})}, {"libraries", nlohmann::json::array({"src/fs_utils.cpp"})}},
			"Compiles and executes a VFS C source file with custom macro definitions, include paths, and extra translation units."
		}
	};
}


bool run_cpp_validator::validate_args_impl(const nlohmann::json &args, const agentlib::tool_context &ctx, std::string &out_error) const

{
	parsed_args_ = run_cpp_args{};

	if (args.contains("code") && args["code"].is_string()) {
		parsed_args_.code = args["code"].get<std::string>();
	}
	if (args.contains("path") && args["path"].is_string()) {
		parsed_args_.path = args["path"].get<std::string>();
	}

	if (parsed_args_.code.empty() && parsed_args_.path.empty()) {
		out_error = "Either 'code' or 'path' must be provided to run_cpp.";
		return false;
	}

	if (!parsed_args_.path.empty()) {
		if (!ctx.fs_security.validate_access(parsed_args_.path, agentlib::access_type::read, parsed_args_.safe_path, out_error)) {
			return false;
		}
	}

	if (args.contains("std") && args["std"].is_string()) {
		parsed_args_.std_ver = args["std"].get<std::string>();
		static const std::set<std::string> valid_stds = {
			"c++23", "c++20", "c++17", "c++14", "c++11",
			"c17", "c11", "c99", "c89", "gnu17", "gnu11"
		};
		if (!valid_stds.contains(parsed_args_.std_ver)) {
			out_error = "Unsupported C/C++ standard version: " + parsed_args_.std_ver;
			return false;
		}
	} else if (!parsed_args_.path.empty() && (parsed_args_.path.ends_with(".c") || parsed_args_.path.ends_with(".C"))) {
		parsed_args_.std_ver = "c17";
	}



	if (args.contains("includes") && args["includes"].is_array()) {
		for (const auto &item : args["includes"]) {
			if (item.is_string()) {
				parsed_args_.includes.push_back(item.get<std::string>());
			}
		}
	}

	if (args.contains("defines") && args["defines"].is_array()) {
		for (const auto &item : args["defines"]) {
			if (item.is_string()) {
				parsed_args_.defines.push_back(item.get<std::string>());
			}
		}
	}

	if (args.contains("libraries") && args["libraries"].is_array()) {
		for (const auto &item : args["libraries"]) {
			if (item.is_string()) {
				parsed_args_.libraries.push_back(item.get<std::string>());
			}
		}
	}

	if (args.contains("timeout") && args["timeout"].is_number_integer()) {
		parsed_args_.timeout = args["timeout"].get<int>();
		if (parsed_args_.timeout < 1 || parsed_args_.timeout > 60) {
			out_error = "Timeout must be between 1 and 60 seconds.";
			return false;
		}
	}

	return true;
}

std::unique_ptr<agentlib::llm_tool> run_cpp_validator::create_tool_impl(const nlohmann::json & /*args*/) const
{
	return std::make_unique<run_cpp_tool>(parsed_args_);
}

REGISTER_TOOL(run_cpp_validator)

} // namespace tools
