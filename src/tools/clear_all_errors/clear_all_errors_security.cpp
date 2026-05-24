#include "../../agentlib/tool_registry.h"
#include "../../build_error_manager.h"
#include "clear_all_errors.h"

namespace tools
{

REGISTER_TOOL(clear_all_errors_security)

nlohmann::json clear_all_errors_security::get_parameters_schema() const
{
	return {{"type", "object"}, {"properties", nlohmann::json::object()}, {"required", nlohmann::json::array()}};
}

bool clear_all_errors_security::validate_args_impl(const nlohmann::json & /*args*/, const agentlib::tool_context & /*ctx*/,
						   std::string & /*out_error*/) const
{
	return true;
}

std::unique_ptr<agentlib::llm_tool> clear_all_errors_security::create_tool_impl(const nlohmann::json & /*args*/) const
{
	return std::make_unique<clear_all_errors_tool>();
}
} // namespace tools
