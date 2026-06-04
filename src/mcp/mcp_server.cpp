#include "mcp_server.h"

namespace agentlib
{

void mcp_server::auto_detect_type()
{
	mcp_type_ = "other";
	if (command_ == "uv" || command_ == "uvx") {
		mcp_type_ = "uv";
	} else if (command_ == "npx" || command_ == "npm" || command_ == "node") {
		mcp_type_ = "npm";
	} else if (command_ == "python" || command_ == "python3") {
		mcp_type_ = "python";
	} else {
		for (const auto &arg : args_) {
			if (arg.ends_with(".py")) {
				mcp_type_ = "python";
				break;
			} else if (arg.ends_with(".js") || arg.ends_with(".mjs") || arg.ends_with(".ts")) {
				mcp_type_ = "npm";
				break;
			}
		}
	}
}

} // namespace agentlib
