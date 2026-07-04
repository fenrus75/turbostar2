#include "plugins/image_basic/image_resize_validator.h"
#include <nlohmann/json.hpp>
#include "agentlib/tool_registry.h"
#include "images/image_manager.h"

namespace tools
{

struct image_resize_raw_args {
	std::string name;
	std::optional<int> newX;
	std::optional<int> newY;
	std::optional<double> ratio;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(image_resize_raw_args, name, newX, newY, ratio);

bool image_resize_validator::validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context & /*ctx*/,
						    std::string &out_error) const
{
	try {
		image_resize_raw_args parsed = raw_json.get<image_resize_raw_args>();
		if (parsed.name.empty()) {
			out_error = "Image name/URI cannot be empty.";
			return false;
		}

		std::string resolved = images::image_manager::get_instance().resolve_uri(parsed.name);
		if (resolved.empty()) {
			out_error = "Image URI could not be resolved: " + parsed.name;
			return false;
		}

		if (!parsed.newX.has_value() && !parsed.newY.has_value() && !parsed.ratio.has_value()) {
			out_error = "Must specify at least one resizing target: newX, newY, or ratio.";
			return false;
		}

		if (parsed.newX.has_value() && *parsed.newX <= 0) {
			out_error = "newX must be a positive integer.";
			return false;
		}
		if (parsed.newY.has_value() && *parsed.newY <= 0) {
			out_error = "newY must be a positive integer.";
			return false;
		}
		if (parsed.ratio.has_value() && *parsed.ratio <= 0.0) {
			out_error = "ratio must be positive.";
			return false;
		}

		args_.original_uri = parsed.name;
		args_.safe_path = resolved;
		args_.newX = parsed.newX;
		args_.newY = parsed.newY;
		args_.ratio = parsed.ratio;

		return true;
	} catch (const std::exception &e) {
		out_error = "Invalid arguments: " + std::string(e.what());
		return false;
	}
}

std::unique_ptr<agentlib::llm_tool> image_resize_validator::create_tool_impl(const nlohmann::json & /*raw_json*/) const
{
	return std::make_unique<image_resize_tool>(args_);
}

} // namespace tools

extern "C" {
void register_image_resize(void)
{
	agentlib::tool_registry::get_instance().register_validator([]() { return std::make_unique<tools::image_resize_validator>(); });
}

void unregister_image_resize(void)
{
	agentlib::tool_registry::get_instance().unregister_validator("image_resize");
}
}
