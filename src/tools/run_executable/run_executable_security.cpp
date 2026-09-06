#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include "agentlib/tool_registry.h"
#include "agentlib/tool_validator.h"
#include "fs_utils.h"
#include "project_manager.h"
#include "run_executable.h"

namespace tools
{

class run_executable_validator : public agentlib::tool_validator
{
      public:
	bool is_pure() const override
	{
		return false;
	} // Spawns external processes

	std::string get_name() const override
	{
		return "run_executable";
	}

	std::string get_description() const override
	{
		return "Runs a binary/executable located within the project directory (or the configured main application if omitted), optionally under GDB debugging with split screen or CPU performance sampling. Unlike run_shell_command, this tool runs directly without requiring user permission or confirmation prompts. Always prefer this tool over run_shell_command for running project executables, binaries, and benchmarks. Returns JSON with app_run_id and gdb_run_id. In GDB mode, send 'continue' to gdb to start application execution.";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {
		    {"type", "object"},
		    {"properties",
		     {{"binary",
		       {{"type", "string"},
			{"description",
			 "Optional relative or build path to the executable within the project directory (e.g., 'crash', 'build/crash', 'turbostar'). Defaults to the configured main application binary."}}},
		      {"executable",
		       {{"type", "string"},
			{"description", "Optional alias for 'binary'."}}},
		      {"path",
		       {{"type", "string"},
			{"description", "Optional alias for 'binary'."}}},
		      {"arguments",
		       {{"type", "string"},
			{"description", "Command line arguments to pass to the application. Optional."}}},
		      {"args",
		       {{"type", "string"},
			{"description", "Command line arguments to pass to the application. Alias for 'arguments'. Optional."}}},
		      {"debugger",
		       {{"type", "boolean"},
			{"description",
			 "If true, starts the application with a split screen debugger (GDB/GDBServer). Defaults to false."},
			{"default", false}}},
		      {"wait_for_time",
		       {{"type", "integer"},
			{"description",
			 "Optional time in seconds to wait for the application to finish after starting. Defaults to 0 (async execution, max 300)."},
			{"default", 0}}},
		      {"collect_performance",
		       {{"type", "boolean"},
			{"description",
			 "If true, enables performance CPU cycle profiling sampling via LD_PRELOAD during execution."},
			{"default", false}}},
		      {"output",
		       {{"type", "boolean"},
			{"description",
			 "Optional. If true, captures and returns the application output emitted during execution. Defaults to false."},
			{"default", false}}}}}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &untrusted_args_json, const agentlib::tool_context &ctx,
				std::string &out_error) const override
	{
		try {
			std::string untrusted_binary;
			if (untrusted_args_json.contains("binary") && untrusted_args_json["binary"].is_string()) {
				untrusted_binary = untrusted_args_json["binary"].get<std::string>();
			} else if (untrusted_args_json.contains("executable") && untrusted_args_json["executable"].is_string()) {
				untrusted_binary = untrusted_args_json["executable"].get<std::string>();
			} else if (untrusted_args_json.contains("path") && untrusted_args_json["path"].is_string()) {
				untrusted_binary = untrusted_args_json["path"].get<std::string>();
			}

			std::string untrusted_args;
			if (untrusted_args_json.contains("args") && untrusted_args_json["args"].is_string()) {
				untrusted_args = untrusted_args_json["args"].get<std::string>();
			} else if (untrusted_args_json.contains("arguments") && untrusted_args_json["arguments"].is_string()) {
				untrusted_args = untrusted_args_json["arguments"].get<std::string>();
			}

			bool debugger = false;
			if (untrusted_args_json.contains("debugger") && untrusted_args_json["debugger"].is_boolean()) {
				debugger = untrusted_args_json["debugger"].get<bool>();
			}

			int wait_for_time = 0;
			if (untrusted_args_json.contains("wait_for_time") && untrusted_args_json["wait_for_time"].is_number_integer()) {
				wait_for_time = untrusted_args_json["wait_for_time"].get<int>();
			}

			bool collect_performance = false;
			if (untrusted_args_json.contains("collect_performance") && untrusted_args_json["collect_performance"].is_boolean()) {
				collect_performance = untrusted_args_json["collect_performance"].get<bool>();
			}

			bool output = false;
			if (untrusted_args_json.contains("output") && untrusted_args_json["output"].is_boolean()) {
				output = untrusted_args_json["output"].get<bool>();
			}

			if (untrusted_args.length() > 1024) {
				out_error = "Validation Error: args parameter exceeds maximum length of 1024 characters.";
				return false;
			}
			// Check for command injection/chaining characters in args
			for (char c : untrusted_args) {
				if (c == ';' || c == '&' || c == '|' || c == '`' || c == '$' ||
				    c == '<' || c == '>' || c == '(' || c == ')' || c == '\'' || c == '"' ||
				    c == '\\' || c == '\n' || c == '\r') {
					out_error = "Security Violation: Unsafe characters detected in arguments.";
					return false;
				}
			}
			if (wait_for_time < 0 || wait_for_time > 300) {
				out_error = "Validation Error: wait_for_time must be between 0 and 300 seconds.";
				return false;
			}

			if (!untrusted_binary.empty()) {
				if (untrusted_binary.length() > 1024) {
					out_error = "Validation Error: binary parameter exceeds maximum length of 1024 characters.";
					return false;
				}
				if (!fs_utils::is_safe_for_ui(untrusted_binary)) {
					out_error = "Validation Error: Binary path contains invalid or non-printable characters.";
					return false;
				}
				for (char c : untrusted_binary) {
					if (c == ';' || c == '&' || c == '|' || c == '`' || c == '$' ||
					    c == '<' || c == '>' || c == '(' || c == ')' || c == '\'' || c == '"' ||
					    c == '\\' || c == '\n' || c == '\r') {
						out_error = "Security Violation: Unsafe characters detected in binary path.";
						return false;
					}
				}

				if (untrusted_binary.find("..") != std::string::npos) {
					out_error = "Security Violation: Path traversal ('..') is prohibited in binary path.";
					return false;
				}

				std::string project_root = project_manager::get_instance().get_project_root();
				if (project_root.empty()) {
					project_root = ctx.fs_security.get_working_directory().string();
				}
				std::filesystem::path canonical_proj_root = std::filesystem::weakly_canonical(project_root);
				std::string proj_root_str = canonical_proj_root.string();
				if (!proj_root_str.ends_with('/')) {
					proj_root_str += '/';
				}

				std::filesystem::path p(untrusted_binary);
				if (p.is_absolute()) {
					std::filesystem::path canon = std::filesystem::weakly_canonical(p);
					if (!canon.string().starts_with(proj_root_str) && canon != canonical_proj_root) {
						out_error = "Security Violation: Executable must be located within the project directory.";
						return false;
					}
				} else {
					std::string norm = untrusted_binary;
					if (norm.starts_with("./")) {
						norm = norm.substr(2);
					}
					std::filesystem::path canon = std::filesystem::weakly_canonical(canonical_proj_root / norm);
					if (!canon.string().starts_with(proj_root_str) && canon != canonical_proj_root) {
						out_error = "Security Violation: Executable must be located within the project directory.";
						return false;
					}
				}
			}

			args_.binary = untrusted_binary;
			args_.args = untrusted_args;
			args_.debugger = debugger;
			args_.wait_for_time = wait_for_time;
			args_.collect_performance = collect_performance;
			args_.output = output;
			return true;
		} catch (const std::exception &e) {
			out_error = "Argument parsing error: " + std::string(e.what());
			return false;
		}
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json & /*args*/) const override
	{
		return std::make_unique<run_executable_tool>(args_);
	}

      private:
	mutable run_executable_args args_;
};

REGISTER_TOOL(run_executable_validator)

} // namespace tools
