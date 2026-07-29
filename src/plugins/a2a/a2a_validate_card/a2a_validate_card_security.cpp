#include "agentlib/tool_registry.h"
#include "a2a_validate_card.h"
#include <nlohmann/json.hpp>

namespace tools
{

struct a2a_validate_card_raw_args {
	std::string path;
	std::string card_data;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(a2a_validate_card_raw_args, path, card_data);

bool a2a_validate_card_validator::validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx,
						       std::string &out_error) const
{
	try {
		a2a_validate_card_raw_args parsed = raw_json.get<a2a_validate_card_raw_args>();

		if (parsed.path.empty() && parsed.card_data.empty()) {
			out_error = "Either 'path' or 'card_data' parameter must be provided.";
			return false;
		}

		if (!parsed.path.empty()) {
			std::string canonical_path;
			if (!ctx.fs_security.validate_access(parsed.path, agentlib::access_type::read, canonical_path, out_error)) {
				return false;
			}
			args_.requested_path = parsed.path;
			args_.safe_path = canonical_path;
		} else {
			args_.requested_path.clear();
			args_.safe_path.clear();
		}

		args_.card_data = parsed.card_data;
		return true;
	} catch (const std::exception &e) {
		out_error = "Invalid arguments: " + std::string(e.what());
		return false;
	}
}

std::unique_ptr<agentlib::llm_tool> a2a_validate_card_validator::create_tool_impl(const nlohmann::json & /*raw_json*/) const
{
	return std::make_unique<a2a_validate_card_tool>(args_);
}

} // namespace tools

extern "C" {
void register_a2a_validate_card(void)
{
	agentlib::tool_registry::get_instance().register_validator(
	    []() { return std::make_unique<tools::a2a_validate_card_validator>(); });
}

void unregister_a2a_validate_card(void)
{
	agentlib::tool_registry::get_instance().unregister_validator("a2a_validate_card");
}
}
