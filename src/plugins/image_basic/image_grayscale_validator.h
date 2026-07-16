#pragma once

#include "agentlib/tool_validator.h"
#include "plugins/image_basic/image_grayscale_tool.h"

namespace tools
{

class image_grayscale_validator : public agentlib::tool_validator
{
      public:
	image_grayscale_validator() = default;
	~image_grayscale_validator() override = default;

	bool is_pure() const override { return false; }
	std::string get_name() const override { return "image_grayscale"; }
	std::string get_description() const override
	{
		return "Converts an image to grayscale. If output is specified, saves the grayscale image as a new image alias; otherwise, converts in place.";
	}
	std::string get_family() const override { return "image"; }
	nlohmann::json get_parameters_schema() const override
	{
		return {
		    {"type", "object"},
		    {"properties",
		     {{"name", {{"type", "string"}, {"description", "The developer-assigned name alias (e.g. 'logo') or the full VFS URI (e.g. 'images://by-sha256/<hash>') of the source image."}}},
		      {"output", {{"type", "string"}, {"description", "Optional. A new friendly alias name (e.g. 'logo_gray') to assign to the resulting grayscale image in the VFS. Subsequent tools can reference the image using this alias. If omitted, the source image is modified in-place."}}}}},
		    {"required", nlohmann::json::array({"name"})}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx,
				std::string &out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json &raw_json) const override;

      private:
	mutable image_grayscale_args args_;
};

} // namespace tools
