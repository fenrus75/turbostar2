#pragma once

#include "agentlib/tool_validator.h"
#include "plugins/image_basic/image_rotate_tool.h"

namespace tools
{

class image_rotate_validator : public agentlib::tool_validator
{
      public:
	image_rotate_validator() = default;
	~image_rotate_validator() override = default;

	bool is_pure() const override { return false; }
	std::string get_name() const override { return "image_rotate"; }
	std::string get_description() const override
	{
		return "Rotates an image counter-clockwise by specified degrees. If output is specified, saves the rotated image as a new image alias; otherwise, rotates in place.";
	}
	std::string get_family() const override { return "image"; }
	nlohmann::json get_parameters_schema() const override
	{
		return {
		    {"type", "object"},
		    {"properties",
		     {{"name", {{"type", "string"}, {"description", "The developer-assigned name alias (e.g. 'logo') or the full VFS URI (e.g. 'images://by-sha256/<hash>') of the source image."}}},
		      {"degrees", {{"type", "number"}, {"description", "The number of degrees to rotate counter-clockwise."}}},
		      {"output", {{"type", "string"}, {"description", "Optional. A new friendly alias name (e.g. 'logo_rotated') to assign to the resulting rotated image in the VFS. Subsequent tools can reference the image using this alias. If omitted, the source image is modified in-place."}}}}},
		    {"required", nlohmann::json::array({"name", "degrees"})}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx,
				std::string &out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json &raw_json) const override;

      private:
	mutable image_rotate_args args_;
};

} // namespace tools
