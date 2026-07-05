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
		     {{"name", {{"type", "string"}, {"description", "The name alias or VFS URI of the image."}}},
		      {"output", {{"type", "string"}, {"description", "Optional new alias name or VFS URI to save the grayscale image as."}}}}},
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
