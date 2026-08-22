#pragma once
#include <memory>
#include <string>
#include <vector>
#include "agentlib/tool_validator.h"


namespace tools
{

struct run_cpp_args {
	std::string code;
	std::string path;
	std::string safe_path;
	std::string std_ver{"c++23"};
	std::vector<std::string> includes;
	std::vector<std::string> defines;
	std::vector<std::string> libraries;
	int timeout{10};
};

class run_cpp_tool : public agentlib::llm_tool
{
      public:
	explicit run_cpp_tool(run_cpp_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	run_cpp_args args_;
};

class run_cpp_validator : public agentlib::tool_validator
{
      public:
	std::string get_name() const override
	{
		return "run_cpp";
	}
	std::string get_description() const override
	{
		return "Compiles and executes C++ source code or standalone probe files in a sandboxed environment with libturbocatch.so preloaded to catch crashes. Useful for testing APIs, language features, and logic.";
	}
	nlohmann::json get_parameters_schema() const override;
	std::vector<agentlib::tool_example> get_examples() const override;
	bool is_pure() const override

	{
		return true;
	}

      protected:
	bool validate_args_impl(const nlohmann::json &args, const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json &args) const override;

      private:
	mutable run_cpp_args parsed_args_;
};

} // namespace tools
