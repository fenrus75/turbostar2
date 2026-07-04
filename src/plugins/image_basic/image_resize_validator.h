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
		return "Resizes an image. If a name alias is provided, the resize is done in place.";
	}
	std::string get_family() const override { return "image"; }
	nlohmann::json get_parameters_schema() const override
	{
		return {
		    {"type", "object"},
		    {"properties",
		     {{"name", {{"type", "string"}, {"description", "The name alias or VFS URI of the image (e.g. 'images://by-name/image.png' or 'image.png')."}}},
		      {"newX", {{"type", "integer"}, {"description", "Optional new width in pixels."}}},
		      {"newY", {{"type", "integer"}, {"description", "Optional new height in pixels."}}},
		      {"ratio", {{"type", "number"}, {"description", "Optional scaling ratio (e.g. 0.5 to shrink to 50%)."}}}}},
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
