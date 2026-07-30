#pragma once

#include "agentlib/llm_tool.h"
#include "agentlib/tool_context.h"
#include <string>

namespace tools
{

struct a2a_connect_server_args {
	std::string name;
	std::string url;
	std::string auth_token;
	bool persistent{false};
};

class a2a_connect_server_tool : public agentlib::llm_tool
{
      public:
	explicit a2a_connect_server_tool(a2a_connect_server_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	a2a_connect_server_args args_;
};

} // namespace tools
