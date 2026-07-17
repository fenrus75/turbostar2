#pragma once
#include <string>
#include "agentlib/llm_tool_action.h"

namespace tools
{

struct image_getdata_args {
	std::string filename;
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
