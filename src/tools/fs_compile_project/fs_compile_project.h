#pragma once
#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <unordered_map>
#include "../../agentlib/interactions/terminal.h"
#include "../../agentlib/llm_tool.h"
#include "../../agentlib/tool_validator.h"
#include "config_manager.h"
#include "project_manager.h"

namespace tools
{

struct fs_compile_project_args {
	bool clean{false};
	bool async{false};
	int timeout{600};
	std::string build_dir{};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(fs_compile_project_args, clean, async, timeout, build_dir);

class fs_compile_project_tool : public agentlib::llm_tool
{
      public:
	fs_compile_project_tool(fs_compile_project_args args = {});

	std::shared_ptr<agentlib::agent_interaction> get_interaction() const override;
	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	std::shared_ptr<agentlib::interaction_terminal> interaction_;
	fs_compile_project_args args_;
};

class fs_compile_project_validator : public agentlib::tool_validator
{
      public:
	std::string get_name() const override
	{
		return "fs_compile_project";
	}
	std::string get_description() const override
	{
		return "Compiles the entire project and returns raw console output. Populates the workspace error list. Supports clean "
		       "rebuilds and background execution via the async flag.";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {{"type", "object"},
			{"properties",
			 {{"clean",
			   {{"type", "boolean"},
			    {"description",
			     "Optional. If true, forces a completely clean rebuild before compiling to clear out stale artifacts."}}},
			  {"async",
			   {{"type", "boolean"}, {"description", "Optional. If true, starts the project compilation in the background."}}},
			  {"timeout", {{"type", "integer"}, {"description", "Optional timeout in seconds. Default is 600."}}},
			  {"build_dir",
			   {{"type", "string"},
			    {"description", "Optional build directory path relative to project root (e.g. 'build' or 'build-asan'). "
					    "Defaults to the configured project build directory."}}}}}};
	}

	std::unordered_map<std::string, std::string> get_custom_parameter_aliases() const override
	{
		return {{"build_directory", "build_dir"}, {"builddir", "build_dir"}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &untrusted_args, const agentlib::tool_context &ctx,
				std::string &out_error) const override
	{
		try {
			args_ = untrusted_args.get<fs_compile_project_args>();
			if (!args_.build_dir.empty()) {
				std::string untrusted_build_dir = args_.build_dir;
				std::string safe_build_dir;
				if (!ctx.fs_security.validate_access(untrusted_build_dir, agentlib::access_type::read, safe_build_dir,
								     out_error)) {
					out_error = "Access denied for build_dir '" + untrusted_build_dir + "': " + out_error;
					return false;
				}
				std::error_code ec;
				if (!std::filesystem::is_directory(safe_build_dir, ec)) {
					out_error =
					    std::format("Error: build_dir '{}' does not exist or is not a directory.", untrusted_build_dir);
					return false;
				}
				std::string build_system = config_manager::get_instance().get_build_system();
				if (!project_manager::is_build_directory_valid(safe_build_dir, build_system)) {
					out_error = std::format("Error: build_dir '{}' is not a configured {} build directory (missing "
								"build definition files).",
								untrusted_build_dir, build_system);
					return false;
				}
				args_.build_dir = safe_build_dir;
			}
			return true;
		} catch (const std::exception &e) {
			out_error = "Invalid arguments: " + std::string(e.what());
			return false;
		}
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json & /*args*/) const override
	{
		return std::make_unique<fs_compile_project_tool>(args_);
	}

      private:
	mutable fs_compile_project_args args_;
};

} // namespace tools
