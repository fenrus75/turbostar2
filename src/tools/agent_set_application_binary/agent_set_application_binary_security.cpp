#include <memory>
#include <nlohmann/json.hpp>
#include "../../agentlib/tool_registry.h"
#include "../../agentlib/tool_validator.h"
#include "agent_set_application_binary.h"

namespace tools
{

struct agent_set_application_binary_raw_args {
	std::string path;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(agent_set_application_binary_raw_args, path);

class agent_set_application_binary_validator : public agentlib::tool_validator
{
      public:
	bool is_pure() const override
	{
		return false;
	}

	std::string get_name() const override
	{
		return "agent_set_application_binary";
	}
	std::string get_description() const override
	{
		return "Sets the main application binary/executable path for run and debug operations. Note: The path must be specified relative to the 'build/' directory (e.g., 'turbostar' or 'test_tool_infrastructure').";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {{"type", "object"},
			{"properties", {{"path", {{"type", "string"}, {"description", "The path to the main application executable, relative to the 'build/' directory (e.g., 'turbostar')."}}}}},
			{"required", nlohmann::json::array({"path"})}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &args_json, const agentlib::tool_context & /*ctx*/,
				std::string &out_error) const override
	{
		try {
			agent_set_application_binary_raw_args raw_args = args_json.get<agent_set_application_binary_raw_args>();
			if (raw_args.path.empty()) {
				out_error = "Argument validation error: 'path' cannot be empty.";
				return false;
			}
			args_.path = raw_args.path;
			return true;
		} catch (const std::exception &e) {
			out_error = "Argument parsing error: " + std::string(e.what());
			return false;
		}
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json & /*args*/) const override
	{
		return std::make_unique<agent_set_application_binary_tool>(args_);
	}

      private:
	mutable agent_set_application_binary_args args_;
};

REGISTER_TOOL(agent_set_application_binary_validator)

} // namespace tools
