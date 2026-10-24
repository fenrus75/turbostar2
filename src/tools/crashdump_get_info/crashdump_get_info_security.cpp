#include <memory>
#include <nlohmann/json.hpp>
#include "agentlib/tool_registry.h"
#include "agentlib/tool_validator.h"
#include "crashdump_get_info.h"
#include "utf8.h"

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
		return "Retrieves detailed backtrace analysis and info for a specific crashdump by process ID (PID). If crash_id is omitted or empty, retrieves information for the most recent crash.";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {{"type", "object"},
			{"properties", {{"crash_id", {{"type", "string"}, {"description", "Optional. The Crash ID of the crashed executable. If omitted, defaults to the most recent crash."}}}}},
			{"required", nlohmann::json::array()}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &args_json, const agentlib::tool_context & /*ctx*/,
				std::string &out_error) const override
	{
		try {
			std::string cid;
			if (args_json.contains("crash_id")) {
				if (args_json["crash_id"].is_string()) {
					cid = args_json["crash_id"].get<std::string>();
				} else if (args_json["crash_id"].is_number_integer()) {
					cid = std::to_string(args_json["crash_id"].get<int64_t>());
				} else if (args_json["crash_id"].is_number_unsigned()) {
					cid = std::to_string(args_json["crash_id"].get<uint64_t>());
				} else if (!args_json["crash_id"].is_null()) {
					out_error = "Validation Error: crash_id must be a string or integer.";
					return false;
				}
			}
			if (cid.length() > 128) {
				out_error = "Validation Error: crash_id must be max 128 characters.";
				return false;
			}
			if (!cid.empty() && utf8::is_not_alnum_only(cid, "_-")) {
				out_error = "Security Violation: crash_id contains invalid or unsafe characters.";
				return false;
			}
			args_.crash_id = std::move(cid);
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