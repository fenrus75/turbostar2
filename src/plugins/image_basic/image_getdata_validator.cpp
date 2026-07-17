#include "plugins/image_basic/image_getdata_validator.h"
#include "agentlib/tool_registry.h"
#include <nlohmann/json.hpp>

namespace tools
{

struct image_getdata_raw_args {
	std::string filename;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(image_getdata_raw_args, filename)

bool image_getdata_validator::validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context & /*ctx*/,
						 std::string &out_error) const
{
	try {
		image_getdata_raw_args parsed = raw_json.get<image_getdata_raw_args>();
		if (parsed.filename.empty()) {
			out_error = "filename cannot be empty.";
			return false;
		}
		args_.filename = parsed.filename;
		return true;
	} catch (const std::exception &e) {
		out_error = "Invalid arguments: " + std::string(e.what());
		return false;
	}
}

std::unique_ptr<agentlib::llm_tool> image_getdata_validator::create_tool_impl(const nlohmann::json & /*raw_json*/) const
{
	return std::make_unique<image_getdata_tool>(args_);
}

} // namespace tools

extern "C" {
void register_image_getdata(void)
{
	agentlib::tool_registry::get_instance().register_validator([]() { return std::make_unique<tools::image_getdata_validator>(); });
}

void unregister_image_getdata(void)
{
	agentlib::tool_registry::get_instance().unregister_validator("image_getdata");
}
}
