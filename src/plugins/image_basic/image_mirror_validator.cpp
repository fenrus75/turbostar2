#include "plugins/image_basic/image_mirror_validator.h"
#include <nlohmann/json.hpp>
#include "agentlib/tool_registry.h"
#include "images/image_manager.h"

namespace tools
{

struct image_mirror_raw_args {
	std::string name;
	std::string direction = "horizontal";
	std::optional<std::string> output;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(image_mirror_raw_args, name, direction, output);

bool image_mirror_validator::validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context & /*ctx*/,
						    std::string &out_error) const
{
	try {
		image_mirror_raw_args parsed = raw_json.get<image_mirror_raw_args>();
		if (parsed.name.empty()) {
			out_error = "Image name/URI cannot be empty.";
			return false;
		}

		auto resolve_image_uri = [](const std::string &uri) -> std::string {
			if (uri.starts_with("images://")) {
				return images::image_manager::get_instance().resolve_uri(uri);
			}
			std::string resolved = images::image_manager::get_instance().resolve_uri("images://by-name/" + uri);
			if (!resolved.empty()) return resolved;
			return images::image_manager::get_instance().resolve_uri("images://" + uri);
		};

		std::string resolved = resolve_image_uri(parsed.name);
		if (resolved.empty()) {
			out_error = "Image URI could not be resolved: " + parsed.name;
			return false;
		}

		if (parsed.direction != "horizontal" && parsed.direction != "vertical" && parsed.direction != "both") {
			out_error = "direction must be 'horizontal', 'vertical', or 'both'.";
			return false;
		}

		args_.name = parsed.name;
		args_.safe_path = resolved;
		args_.direction = parsed.direction;
		args_.output = parsed.output;

		return true;
	} catch (const std::exception &e) {
		out_error = "Invalid arguments: " + std::string(e.what());
		return false;
	}
}

std::unique_ptr<agentlib::llm_tool> image_mirror_validator::create_tool_impl(const nlohmann::json & /*raw_json*/) const
{
	return std::make_unique<image_mirror_tool>(args_);
}

} // namespace tools

extern "C" {
void register_image_mirror(void)
{
	agentlib::tool_registry::get_instance().register_validator([]() { return std::make_unique<tools::image_mirror_validator>(); });
}

void unregister_image_mirror(void)
{
	agentlib::tool_registry::get_instance().unregister_validator("image_mirror");
}
}
