#pragma once
#include <string>
#include "agentlib/llm_tool_action.h"

namespace tools
{

struct image_getdata_args {
	// Maximum size (in pixels) of the largest dimension of an ephemeral thumbnail.
	// A 96px-wide image comfortably fits under the default 50 KB size limit while
	// remaining a reasonably sized, legible preview for the model.
	static constexpr int kThumbnailMaxDim = 96;

	std::string filename;
	size_t max_bytes{51200}; // Default 50 KB size limit
	bool thumbnail{false};	 // If true, shrink the image so its largest dimension is <= kThumbnailMaxDim
};

class image_getdata_tool : public agentlib::llm_tool_action
{
      public:
	explicit image_getdata_tool(image_getdata_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	image_getdata_args args_;
};

} // namespace tools
