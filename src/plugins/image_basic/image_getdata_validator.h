#pragma once
#include "agentlib/tool_validator.h"
#include "plugins/image_basic/image_getdata_tool.h"

namespace tools
{

class image_getdata_validator : public agentlib::tool_validator
{
      public:
	image_getdata_validator() = default;
	~image_getdata_validator() override = default;

	bool is_pure() const override { return true; }
	std::string get_name() const override { return "image_getdata"; }
	std::string get_description() const override
	{
		return "Retrieves the binary content of a VFS image, returned as a Base64-encoded Data URL entity.";
	}
	std::string get_family() const override { return "image"; }
	nlohmann::json get_parameters_schema() const override
	{
		return {
		    {"type", "object"},
		    {"properties",
		     {{"filename", {{"type", "string"}, {"description", "The friendly alias name (e.g. 'logo') or full VFS URI (e.g. 'images://by-sha256/<hash>') of the image."}}},
		      {"max_bytes", {{"type", "integer"}, {"description", "Maximum allowed byte size for returned image data (default: 51200 bytes / 50 KB)."}}}}},
		    {"required", nlohmann::json::array({"filename"})}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx,
				std::string &out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json &raw_json) const override;

      private:
	mutable image_getdata_args args_;
};

} // namespace tools
