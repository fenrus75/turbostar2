#include <nlohmann/json.hpp>
#include "a2a_connect_server.h"
#include "agentlib/tool_registry.h"
#include "agentlib/tool_validator.h"
#include "fs_utils.h"

namespace tools
{

struct a2a_connect_server_raw_args {
	std::string name;
	std::string url;
	std::string auth_token;
	bool persistent{false};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(a2a_connect_server_raw_args, name, url, auth_token, persistent);

class a2a_connect_server_validator : public agentlib::tool_validator
{
      public:
	bool is_pure() const override
	{
		return false;
	}

	std::string get_name() const override
	{
		return "a2a_connect_server";
	}

	std::string get_family() const override
	{
		return "base|a2a";
	}

	std::string get_description() const override
	{
		return "Connects to a remote A2A server and registers it for remote subagent invocation (`invoke_subagent`).";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {
		    {"type", "object"},
		    {"properties",
		     {{"name", {{"type", "string"}, {"description", "Unique short name for the server (e.g. 'devpc' or 'gpu_node')."}}},
		      {"url", {{"type", "string"}, {"description", "Base URL of the A2A server (e.g. 'http://devpc.local:7820')."}}},
		      {"auth_token", {{"type", "string"}, {"description", "Optional bearer token or API key for authentication."}}},
		      {"persistent",
		       {{"type", "boolean"},
			{"description", "If true, saves server connection to project-local settings. Defaults to false (ephemeral)."}}}}},
		    {"required", nlohmann::json::array({"name", "url"})}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &args_json, const agentlib::tool_context &, std::string &out_error) const override
	{
		try {
			a2a_connect_server_raw_args raw = args_json.get<a2a_connect_server_raw_args>();
			if (raw.name.empty()) {
				out_error = "Server name cannot be empty.";
				return false;
			}
			if (raw.url.empty()) {
				out_error = "Server URL cannot be empty.";
				return false;
			}
			if (!raw.url.starts_with("http://") && !raw.url.starts_with("https://")) {
				out_error = "Server URL must start with http:// or https://.";
				return false;
			}
			if (raw.url.find("169.254.") != std::string::npos) {
				out_error = "Security Violation: Access to cloud metadata endpoint (169.254.x.x) is blocked.";
				return false;
			}
			if (raw.name.find(':') != std::string::npos || raw.name.find('/') != std::string::npos) {
				out_error = "Server name cannot contain colons or slashes.";
				return false;
			}
			if (!fs_utils::is_safe_for_ui(raw.name)) {
				out_error = "Security Violation: Server name contains unsafe control characters.";
				return false;
			}

			args_.name = raw.name;
			args_.url = raw.url;
			args_.auth_token = raw.auth_token;
			args_.persistent = raw.persistent;
			return true;
		} catch (const std::exception &e) {
			out_error = "Argument parsing error: " + std::string(e.what());
			return false;
		}
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json &) const override
	{
		return std::make_unique<a2a_connect_server_tool>(args_);
	}

      private:
	mutable a2a_connect_server_args args_;
};

REGISTER_TOOL(a2a_connect_server_validator)

} // namespace tools
