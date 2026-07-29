#include "agentlib/tool_registry.h"
#include "a2a_generate_card_with_agent.h"
#include <nlohmann/json.hpp>

namespace tools
{

struct a2a_generate_card_with_agent_raw_args {
	std::string path;
	std::string output_path;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(a2a_generate_card_with_agent_raw_args, path, output_path);

bool a2a_generate_card_with_agent_validator::validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx,
								  std::string &out_error) const
{
	try {
		a2a_generate_card_with_agent_raw_args parsed = raw_json.get<a2a_generate_card_with_agent_raw_args>();

		if (parsed.path.empty()) {
			out_error = "Parameter 'path' must be provided.";
			return false;
		}

		std::string canonical_path;
		if (!ctx.fs_security.validate_access(parsed.path, agentlib::access_type::read, canonical_path, out_error)) {
			return false;
		}

		args_.requested_path = parsed.path;
		args_.safe_path = canonical_path;
		args_.output_path = parsed.output_path;
		return true;
	} catch (const std::exception &e) {
		out_error = "Invalid arguments: " + std::string(e.what());
		return false;
	}
}

std::unique_ptr<agentlib::llm_tool> a2a_generate_card_with_agent_validator::create_tool_impl(const nlohmann::json & /*raw_json*/) const
{
	return std::make_unique<a2a_generate_card_with_agent_tool>(args_);
}

} // namespace tools

extern "C" {
void register_a2a_generate_card_with_agent(void)
{
	agentlib::tool_registry::get_instance().register_validator(
	    []() { return std::make_unique<tools::a2a_generate_card_with_agent_validator>(); });
}

void unregister_a2a_generate_card_with_agent(void)
{
	agentlib::tool_registry::get_instance().unregister_validator("a2a_generate_card_with_agent");
}
}
