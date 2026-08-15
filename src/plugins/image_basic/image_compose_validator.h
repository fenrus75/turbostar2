#pragma once

#include "agentlib/tool_validator.h"
#include "plugins/image_basic/image_compose_tool.h"

namespace tools
{

class image_compose_validator : public agentlib::tool_validator
{
      public:
	image_compose_validator() = default;
	~image_compose_validator() override = default;

	bool is_pure() const override { return true; }
	std::string get_name() const override { return "image_compose"; }
	std::string get_description() const override
	{
		return "Composes (overlays) a small image onto a main destination image at the specified x, y coordinates. If output is specified, saves the composed result as a new image alias; otherwise, modifies the main image in place.";
	}
	std::string get_family() const override { return "image"; }
	nlohmann::json get_parameters_schema() const override
	{
		return {
		    {"type", "object"},
		    {"properties",
		     {{"main_image", {{"type", "string"}, {"description", "The developer-assigned name alias (e.g. 'logo') or the full VFS URI (e.g. 'images://by-sha256/<hash>') of the destination/main image."}}},
		      {"small_image", {{"type", "string"}, {"description", "The developer-assigned name alias (e.g. 'overlay') or the full VFS URI (e.g. 'images://by-sha256/<hash>') of the source/small image to overlay."}}},
		      {"x", {{"type", "integer"}, {"description", "X coordinate offset to place the small image on the main image."}}},
		      {"y", {{"type", "integer"}, {"description", "Y coordinate offset to place the small image on the main image."}}},
		      {"output", {{"type", "string"}, {"description", "Optional. A new friendly alias name (e.g. 'logo_composed') to assign to the resulting composed image in the VFS. Subsequent tools can reference the image using this alias. If omitted, the main image is modified in-place."}}}}},
		    {"required", nlohmann::json::array({"main_image", "small_image", "x", "y"})}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx,
				std::string &out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json &raw_json) const override;

      private:
	mutable image_compose_args args_;
};

} // namespace tools
