#pragma once

#include "agentlib/tool_validator.h"
#include "plugins/image_basic/image_crop_tool.h"

namespace tools
{

class image_crop_validator : public agentlib::tool_validator
{
      public:
	image_crop_validator() = default;
	~image_crop_validator() override = default;

	bool is_pure() const override { return false; }
	std::string get_name() const override { return "image_crop"; }
	std::string get_description() const override
	{
		return "Crops a rectangular selection from an image. If output is specified, saves the cropped selection as a new image alias; otherwise, crops in place.";
	}
	std::string get_family() const override { return "image"; }
	nlohmann::json get_parameters_schema() const override
	{
		return {
		    {"type", "object"},
		    {"properties",
		     {{"name", {{"type", "string"}, {"description", "The developer-assigned name alias (e.g. 'logo') or the full VFS URI (e.g. 'images://by-sha256/<hash>') of the source image."}}},
		      {"width", {{"type", "integer"}, {"description", "Width of the crop selection in pixels."}}},
		      {"height", {{"type", "integer"}, {"description", "Height of the crop selection in pixels."}}},
		      {"x", {{"type", "integer"}, {"description", "X coordinate offset of the selection."}}},
		      {"y", {{"type", "integer"}, {"description", "Y coordinate offset of the selection."}}},
		      {"output", {{"type", "string"}, {"description", "Optional. A new friendly alias name (e.g. 'logo_cropped') to assign to the resulting cropped image in the VFS. Subsequent tools can reference the image using this alias. If omitted, the source image is modified in-place."}}}}},
		    {"required", nlohmann::json::array({"name", "width", "height", "x", "y"})}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx,
				std::string &out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json &raw_json) const override;

      private:
	mutable image_crop_args args_;
};

} // namespace tools
