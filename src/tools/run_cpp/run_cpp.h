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
	std::vector<std::string> cflags;
	std::vector<std::string> ldflags;
	std::vector<std::string> sources;
	std::vector<std::string> packages;
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
	std::string get_description() const override;

	nlohmann::json get_parameters_schema() const override;
	std::vector<agentlib::tool_example> get_examples() const override;
	bool is_pure() const override
	{
		return true;
	}

	std::unordered_map<std::string, std::string> get_custom_parameter_aliases() const override;

      protected:
	bool validate_args_impl(const nlohmann::json &args, const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json &args) const override;

      private:
	mutable run_cpp_args parsed_args_;
};

} // namespace tools
