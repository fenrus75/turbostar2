#include "plugins/image_basic/image_threshold_validator.h"
#include <nlohmann/json.hpp>
#include "agentlib/tool_registry.h"
#include "images/image_manager.h"

namespace tools
{

struct image_threshold_raw_args {
	std::string name;
	std::optional<double> level;
	int windowWidth = 16;
	int windowHeight = 16;
	double offset = 0.0;
	std::optional<std::string> output;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(image_threshold_raw_args, name, level, windowWidth, windowHeight, offset, output);

bool image_threshold_validator::validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context & /*ctx*/,
						    std::string &out_error) const
{
	try {
		image_threshold_raw_args parsed = raw_json.get<image_threshold_raw_args>();
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

		if (parsed.windowWidth <= 0) {
			out_error = "windowWidth must be a positive integer.";
			return false;
		}
		if (parsed.windowHeight <= 0) {
			out_error = "windowHeight must be a positive integer.";
			return false;
		}

		args_.name = parsed.name;
		args_.safe_path = resolved;
		args_.level = parsed.level;
		args_.windowWidth = parsed.windowWidth;
		args_.windowHeight = parsed.windowHeight;
		args_.offset = parsed.offset;
		args_.output = parsed.output;

		return true;
	} catch (const std::exception &e) {
		out_error = "Invalid arguments: " + std::string(e.what());
		return false;
	}
}

std::unique_ptr<agentlib::llm_tool> image_threshold_validator::create_tool_impl(const nlohmann::json & /*raw_json*/) const
{
	return std::make_unique<image_threshold_tool>(args_);
}

} // namespace tools

extern "C" {
void register_image_threshold(void)
{
	agentlib::tool_registry::get_instance().register_validator([]() { return std::make_unique<tools::image_threshold_validator>(); });
}

void unregister_image_threshold(void)
{
	agentlib::tool_registry::get_instance().unregister_validator("image_threshold");
}
}
