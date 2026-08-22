#include "plugins/image_basic/image_compose_validator.h"
#include <nlohmann/json.hpp>
#include "agentlib/tool_registry.h"
#include "images/image_manager.h"

namespace tools
{

struct image_compose_raw_args {
	std::string main_image;
	std::string small_image;
	int x = 0;
	int y = 0;
	std::optional<std::string> output;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(image_compose_raw_args, main_image, small_image, x, y, output);

bool image_compose_validator::validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context & /*ctx*/,
						  std::string &out_error) const
{
	try {
		image_compose_raw_args parsed = raw_json.get<image_compose_raw_args>();
		if (parsed.main_image.empty()) {
			out_error = "Main image name/URI cannot be empty.";
			return false;
		}
		if (parsed.small_image.empty()) {
			out_error = "Small image name/URI cannot be empty.";
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

		std::string resolved_main = resolve_image_uri(parsed.main_image);
		if (resolved_main.empty()) {
			out_error = "Main image URI could not be resolved: " + parsed.main_image;
			return false;
		}

		std::string resolved_small = resolve_image_uri(parsed.small_image);
		if (resolved_small.empty()) {
			out_error = "Small image URI could not be resolved: " + parsed.small_image;
			return false;
		}

		if (parsed.x < 0) {
			out_error = "x coordinate must be non-negative.";
			return false;
		}
		if (parsed.y < 0) {
			out_error = "y coordinate must be non-negative.";
			return false;
		}

		args_.main_image = parsed.main_image;
		args_.safe_path_main = resolved_main;
		args_.small_image = parsed.small_image;
		args_.safe_path_small = resolved_small;
		args_.x = parsed.x;
		args_.y = parsed.y;
		args_.output = parsed.output;

		return true;
	} catch (const std::exception &e) {
		out_error = "Invalid arguments: " + std::string(e.what());
		return false;
	}
}

std::unique_ptr<agentlib::llm_tool> image_compose_validator::create_tool_impl(const nlohmann::json & /*raw_json*/) const
{
	return std::make_unique<image_compose_tool>(args_);
}

std::vector<agentlib::tool_example> image_compose_validator::get_examples() const
{
	return {
		{
			"Overlay Small Image onto Destination Canvas Alias",
			nlohmann::json{{"main_image", "canvas"}, {"small_image", "logo"}, {"output", "canvas_with_logo"}, {"x", 50}, {"y", 50}},
			"Overlays VFS image alias 'logo' onto 'canvas' at offset (50, 50), saving to new alias 'canvas_with_logo'."
		}
	};
}

} // namespace tools


extern "C" {
void register_image_compose(void)
{
	agentlib::tool_registry::get_instance().register_validator([]() { return std::make_unique<tools::image_compose_validator>(); });
}

void unregister_image_compose(void)
{
	agentlib::tool_registry::get_instance().unregister_validator("image_compose");
}
}
