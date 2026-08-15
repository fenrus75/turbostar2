#pragma once

#include "agentlib/tool_validator.h"
#include "plugins/image_basic/image_mirror_tool.h"

namespace tools
{

class image_mirror_validator : public agentlib::tool_validator
{
      public:
	image_mirror_validator() = default;
	~image_mirror_validator() override = default;

	bool is_pure() const override { return true; }
	std::string get_name() const override { return "image_mirror"; }
	std::string get_description() const override
	{
		return "Mirrors (flips/flops) an image horizontally, vertically, or both. If output is specified, saves the mirrored image as a new image alias; otherwise, mirrors in place.";
	}
	std::string get_family() const override { return "image"; }
	nlohmann::json get_parameters_schema() const override
	{
		return {
		    {"type", "object"},
		    {"properties",
		     {{"name", {{"type", "string"}, {"description", "The developer-assigned name alias (e.g. 'logo') or the full VFS URI (e.g. 'images://by-sha256/<hash>') of the source image."}}},
		      {"direction", {{"type", "string"}, {"enum", {"horizontal", "vertical", "both"}}, {"description", "The direction to mirror: 'horizontal' (flop), 'vertical' (flip), or 'both' (both flip and flop). Default is 'horizontal'."}}},
		      {"output", {{"type", "string"}, {"description", "Optional. A new friendly alias name (e.g. 'logo_mirrored') to assign to the resulting mirrored image in the VFS. Subsequent tools can reference the image using this alias. If omitted, the source image is modified in-place."}}}}},
		    {"required", nlohmann::json::array({"name"})}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx,
				std::string &out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json &raw_json) const override;

      private:
	mutable image_mirror_args args_;
};

} // namespace tools
