#include "plugins/image_basic/image_crop_validator.h"
#include <nlohmann/json.hpp>
#include "agentlib/tool_registry.h"
#include "images/image_manager.h"

namespace tools
{

struct image_crop_raw_args {
	std::string name;
	int width = 0;
	int height = 0;
	int x = 0;
	int y = 0;
	std::optional<std::string> output;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(image_crop_raw_args, name, width, height, x, y, output);

bool image_crop_validator::validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context & /*ctx*/,
						  std::string &out_error) const
{
	try {
		image_crop_raw_args parsed = raw_json.get<image_crop_raw_args>();
		if (parsed.name.empty()) {
			out_error = "Source image name/URI cannot be empty.";
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
			out_error = "Source image URI could not be resolved: " + parsed.name;
			return false;
		}

		if (parsed.width <= 0) {
			out_error = "width must be a positive integer.";
			return false;
		}
		if (parsed.height <= 0) {
			out_error = "height must be a positive integer.";
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

		args_.name = parsed.name;
		args_.safe_path = resolved;
		args_.width = parsed.width;
		args_.height = parsed.height;
		args_.x = parsed.x;
		args_.y = parsed.y;
		args_.output = parsed.output;

		return true;
	} catch (const std::exception &e) {
		out_error = "Invalid arguments: " + std::string(e.what());
		return false;
	}
}

std::unique_ptr<agentlib::llm_tool> image_crop_validator::create_tool_impl(const nlohmann::json & /*raw_json*/) const
{
	return std::make_unique<image_crop_tool>(args_);
}

std::vector<agentlib::tool_example> image_crop_validator::get_examples() const
{
	return {
		{
			"Crop Bounding Sub-Region from Image Alias",
			nlohmann::json{{"name", "logo"}, {"output", "logo_crop"}, {"x", 10}, {"y", 10}, {"width", 100}, {"height", 100}},
			"Crops 100x100 sub-region from 'logo' starting at (10, 10), saving result to new VFS alias 'logo_crop'."
		}
	};
}

} // namespace tools


extern "C" {
void register_image_crop(void)
{
	agentlib::tool_registry::get_instance().register_validator([]() { return std::make_unique<tools::image_crop_validator>(); });
}

void unregister_image_crop(void)
{
	agentlib::tool_registry::get_instance().unregister_validator("image_crop");
}
}
