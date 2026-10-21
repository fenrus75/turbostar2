#include "run_cpp.h"
#include "config_manager.h"
#include "agentlib/file_security_manager.h"
#include "agentlib/tool_registry.h"
#include <set>

namespace tools
{

std::string run_cpp_validator::get_description() const
{
	std::string desc = "Compiles and executes C/C++ source code snippets or standalone probe files in a sandboxed environment. Use this to test C/C++ logic, verify API behavior, or run isolated probes without modifying codebase files. Supports compiler flags (cflags), linker flags (ldflags), extra translation units (sources), and custom VFS paths (tmp://, include://).";
	if (config_manager::get_instance().is_allow_code_execution_network()) {
		desc += " Code execution has network access enabled.";
	} else {
		desc += " Code execution is strictly offline without network access.";
	}
	return desc;
}

nlohmann::json run_cpp_validator::get_parameters_schema() const
{
	return {
		{"type", "object"},
		{"properties", {
			{"code", {{"type", "string"}, {"description", "Inline C/C++ source code to compile and run. Omitting main() relies on automatic entry point wrapping, which is for trivial cases only; for non-trivial code (headers, functions, structs), provide complete source including main()."}}},
			{"path", {{"type", "string"}, {"description", "Optional relative workspace path or VFS URI to a .cpp or .c source file (e.g. 'src/main.cpp', 'tmp://probe.c'). Fully resolves VFS URIs (tmp://, include://, skills://)."}}},
			{"cflags", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Optional array of compiler flags (e.g. ['-O2', '-Wall', '-DDEBUG=1', '-Iinclude']). Supports VFS URI include paths (e.g. '-Itmp://', '-Iinclude://'). Defaults to the project's configured language standard (e.g. '-std=c++23'), or '-std=c17' for C source files."}}},
			{"ldflags", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Optional array of linker library flags (e.g. ['-lncursesw', '-lsqlite3', '-L/usr/lib']) appended after source files on the compiler command line."}}},
			{"sources", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Optional array of extra translation unit source files (e.g. ['src/fs_utils.cpp', 'tmp://helper.cpp']) to compile and link into the probe binary."}}},
			{"timeout", {{"type", "integer"}, {"default", 10}, {"description", "Maximum execution runtime in seconds (1 to 60). Defaults to 10."}}}
		}}
	};
}

std::vector<agentlib::tool_example> run_cpp_validator::get_examples() const
{
	return {
		{
			"Inline Modern C++ Probe",
			nlohmann::json{{"code", "std::cout << \"Sum: \" << (20 + 22) << std::endl;"}},
			"Compiles and executes an inline C++ snippet using project language standard."
		},
		{
			"C11 API Probe with Library Flags",
			nlohmann::json{{"code", "printf(\"SQLite: %s\\n\", sqlite3_libversion());"}, {"cflags", nlohmann::json::array({"-std=c17"})}, {"ldflags", nlohmann::json::array({"-lsqlite3"})}},
			"Compiles and executes a C11 snippet linked against libsqlite3."
		},
		{
			"VFS Source Probe with Custom Flags & Extra Translation Unit",
			nlohmann::json{{"path", "tmp://probe.c"}, {"cflags", nlohmann::json::array({"-DDEBUG=1", "-Itmp://"})}, {"sources", nlohmann::json::array({"tmp://helper.c"})}},
			"Compiles and executes a VFS C source file with custom macro definitions, VFS include paths, and an extra translation unit."
		}
	};
}

std::unordered_map<std::string, std::string> run_cpp_validator::get_custom_parameter_aliases() const
{
	return {
		{"CFLAGS", "cflags"},
		{"cflag", "cflags"},
		{"compile_flags", "cflags"},
		{"flags", "cflags"},
		{"LDFLAGS", "ldflags"},
		{"ldflag", "ldflags"},
		{"link_flags", "ldflags"},
		{"linker_flags", "ldflags"},
		{"SOURCES", "sources"},
		{"source", "sources"},
		{"source_files", "sources"},
		{"extra_sources", "sources"}
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

	auto parse_array = [&](std::initializer_list<const char *> names, auto handler) {
		const nlohmann::json *arr = nullptr;
		for (const char *name : names) {
			if (args.contains(name) && args[name].is_array()) {
				arr = &args[name];
				break;
			}
		}
		if (arr) {
			for (const auto &item : *arr) {
				if (item.is_string()) {
					if (!handler(item.get<std::string>())) return false;
				}
			}
		}
		return true;
	};

	if (!parse_array({"cflags", "Cflags", "CFLAGS"}, [&](const std::string &flag) {
		if (flag.starts_with("-specs=") || flag.starts_with("@")) {
			out_error = "Disallowed compiler flag: " + flag;
			return false;
		}
		parsed_args_.cflags.push_back(flag);
		return true;
	})) return false;

	if (!parse_array({"ldflags", "Ldflags", "LDFLAGS"}, [&](const std::string &flag) {
		if (flag.starts_with("-specs=") || flag.starts_with("@")) {
			out_error = "Disallowed linker flag: " + flag;
			return false;
		}
		parsed_args_.ldflags.push_back(flag);
		return true;
	})) return false;

	if (!parse_array({"sources", "Sources", "SOURCES"}, [&](const std::string &src_path) {
		std::string safe_src;
		if (!ctx.fs_security.validate_access(src_path, agentlib::access_type::read, safe_src, out_error)) {
			return false;
		}
		parsed_args_.sources.push_back(safe_src.empty() ? src_path : safe_src);
		return true;
	})) return false;

	// Backward-compatible fallback for defines -> cflags
	parse_array({"defines", "Defines", "DEFINES"}, [&](const std::string &def) {
		std::string flag = def.starts_with("-D") ? def : ("-D" + def);
		parsed_args_.cflags.push_back(flag);
		return true;
	});

	// Backward-compatible fallback for includes -> cflags
	parse_array({"includes", "Includes", "INCLUDES"}, [&](const std::string &inc) {
		parsed_args_.cflags.push_back("-I" + inc);
		return true;
	});

	// Backward-compatible fallback for libraries -> ldflags / sources
	parse_array({"libraries", "Libraries", "LIBRARIES"}, [&](const std::string &lib) {
		if (lib.starts_with("-")) {
			parsed_args_.ldflags.push_back(lib);
		} else {
			std::string safe_src;
			if (!ctx.fs_security.validate_access(lib, agentlib::access_type::read, safe_src, out_error)) {
				return false;
			}
			parsed_args_.sources.push_back(safe_src.empty() ? lib : safe_src);
		}
		return true;
	});

	// Backward-compatible fallback for std -> cflags
	std::string std_arg;
	if (args.contains("std") && args["std"].is_string()) std_arg = args["std"].get<std::string>();
	else if (args.contains("Std") && args["Std"].is_string()) std_arg = args["Std"].get<std::string>();
	if (!std_arg.empty()) {
		parsed_args_.cflags.push_back("-std=" + std_arg);
	}

	// Auto-detect / resolve language standard if not explicitly supplied in cflags
	bool has_std = false;
	for (const auto &flag : parsed_args_.cflags) {
		if (flag.starts_with("-std=")) {
			has_std = true;
			break;
		}
	}

	if (!has_std) {
		bool is_c = !parsed_args_.path.empty() &&
			    (parsed_args_.path.ends_with(".c") || parsed_args_.path.ends_with(".C"));
		if (is_c) {
			parsed_args_.cflags.insert(parsed_args_.cflags.begin(), "-std=c17");
		} else {
			std::string proj_ver = config_manager::get_instance().get_primary_language_version();
			std::string std_flag = "-std=c++23";
			if (!proj_ver.empty()) {
				std::string norm;
				for (char ch : proj_ver) {
					if (!std::isspace(static_cast<unsigned char>(ch))) {
						norm += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
					}
				}
				if (!norm.empty()) {
					if (norm.starts_with("-std=")) {
						std_flag = norm;
					} else {
						std_flag = "-std=" + norm;
					}
				}
			}
			parsed_args_.cflags.insert(parsed_args_.cflags.begin(), std_flag);
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
