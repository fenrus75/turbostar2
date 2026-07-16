#pragma once

#include "agentlib/tool_validator.h"
#include "plugins/image_basic/image_resize_tool.h"

namespace tools
{

class image_resize_validator : public agentlib::tool_validator
{
      public:
	image_resize_validator() = default;
	~image_resize_validator() override = default;

	bool is_pure() const override { return false; }
	std::string get_name() const override { return "image_resize"; }
	std::string get_description() const override
	{
		return "Resizes an image. If output is specified, saves the resized image as a new image alias; otherwise, resizes in place.";
	}
	std::string get_family() const override { return "image"; }
	nlohmann::json get_parameters_schema() const override
	{
		return {
		    {"type", "object"},
		    {"properties",
		     {{"name", {{"type", "string"}, {"description", "The developer-assigned name alias (e.g. 'logo') or the full VFS URI (e.g. 'images://by-sha256/<hash>') of the source image."}}},
		      {"newX", {{"type", "integer"}, {"description", "Optional new width in pixels."}}},
		      {"newY", {{"type", "integer"}, {"description", "Optional new height in pixels."}}},
		      {"ratio", {{"type", "number"}, {"description", "Optional scaling ratio (e.g. 0.5 to shrink to 50%)."}}},
		      {"output", {{"type", "string"}, {"description", "Optional. A new friendly alias name (e.g. 'logo_resized') to assign to the resulting resized image in the VFS. Subsequent tools can reference the image using this alias. If omitted, the source image is modified in-place."}}}}},
		    {"required", nlohmann::json::array({"name"})}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx,
				std::string &out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json &raw_json) const override;

      private:
	mutable image_resize_args args_;
};

} // namespace tools
