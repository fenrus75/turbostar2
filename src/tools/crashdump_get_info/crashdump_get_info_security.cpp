#include <memory>
#include <nlohmann/json.hpp>
#include "../../agentlib/tool_registry.h"
#include "../../agentlib/tool_validator.h"
#include "crashdump_get_info.h"

namespace tools
{

struct crashdump_get_info_raw_args {
	std::string crash_id;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(crashdump_get_info_raw_args, crash_id);

class crashdump_get_info_validator : public agentlib::tool_validator
{
      public:
	bool is_pure() const override
	{
		return true;
	} // Only reading data

	std::string get_name() const override
	{
		return "crashdump_get_info";
	}
	std::string get_description() const override
	{
		return "Retrieves the detailed backtrace and memory map of a specific crashdump by its ID.";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {{"type", "object"},
			{"properties", {{"crash_id", {{"type", "string"}, {"description", "The Crash ID of the crashed executable."}}}}},
			{"required", nlohmann::json::array({"crash_id"})}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &args_json, const agentlib::tool_context & /*ctx*/,
				std::string &out_error) const override
	{
		try {
			crashdump_get_info_raw_args raw_args = args_json.get<crashdump_get_info_raw_args>();
			if (raw_args.crash_id.empty()) {
				out_error = "Crash ID cannot be empty.";
				return false;
			}
			args_.crash_id = raw_args.crash_id;
			return true;
		} catch (const std::exception &e) {
			out_error = "Argument parsing error: " + std::string(e.what());
			return false;
		}
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json & /*args*/) const override
	{
		return std::make_unique<crashdump_get_info_tool>(args_);
	}

      private:
	mutable crashdump_get_info_args args_;
};

REGISTER_TOOL(crashdump_get_info_validator)

} // namespace tools