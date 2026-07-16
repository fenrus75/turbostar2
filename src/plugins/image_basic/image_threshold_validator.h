#pragma once

#include "agentlib/tool_validator.h"
#include "plugins/image_basic/image_threshold_tool.h"

namespace tools
{

class image_threshold_validator : public agentlib::tool_validator
{
      public:
	image_threshold_validator() = default;
	~image_threshold_validator() override = default;

	bool is_pure() const override { return false; }
	std::string get_name() const override { return "image_threshold"; }
	std::string get_description() const override
	{
		return "Applies standard or adaptive thresholding (binarization) to an image. If output is specified, saves the result as a new image alias; otherwise, modifies in place.";
	}
	std::string get_family() const override { return "image"; }
	nlohmann::json get_parameters_schema() const override
	{
		return {
		    {"type", "object"},
		    {"properties",
		     {{"name", {{"type", "string"}, {"description", "The developer-assigned name alias (e.g. 'logo') or the full VFS URI (e.g. 'images://by-sha256/<hash>') of the source image."}}},
		      {"level", {{"type", "number"}, {"description", "Optional standard threshold level (typically 0.0 to 1.0 or pixel value). If specified, standard thresholding is used."}}},
		      {"windowWidth", {{"type", "integer"}, {"description", "Optional neighborhood width for adaptive thresholding. Default is 16."}}},
		      {"windowHeight", {{"type", "integer"}, {"description", "Optional neighborhood height for adaptive thresholding. Default is 16."}}},
		      {"offset", {{"type", "number"}, {"description", "Optional localized offset constant for adaptive thresholding. Default is 0.0."}}},
		      {"output", {{"type", "string"}, {"description", "Optional. A new friendly alias name (e.g. 'logo_binarized') to assign to the resulting binarized image in the VFS. Subsequent tools can reference the image using this alias. If omitted, the source image is modified in-place."}}}}},
		    {"required", nlohmann::json::array({"name"})}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx,
				std::string &out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json &raw_json) const override;

      private:
	mutable image_threshold_args args_;
};

} // namespace tools
