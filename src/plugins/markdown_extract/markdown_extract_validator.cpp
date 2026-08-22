#include "markdown_extract_validator.h"
#include "agentlib/tool_registry.h"

namespace tools {

bool markdown_extract_validator::validate_args_impl(const nlohmann::json &args, const agentlib::tool_context &ctx, std::string &out_error) const
{
	if (!args.contains("path") || !args["path"].is_string()) {
		out_error = "Parameter 'path' is required and must be a string.";
		return false;
	}
	args_.path = args["path"].get<std::string>();
	if (args_.path.empty()) {
		out_error = "Parameter 'path' cannot be empty.";
		return false;
	}

	if (!args.contains("query") || !args["query"].is_string()) {
		out_error = "Parameter 'query' is required and must be a string.";
		return false;
	}
	args_.query = args["query"].get<std::string>();
	if (args_.query.empty()) {
		out_error = "Parameter 'query' cannot be empty.";
		return false;
	}

	if (args.contains("output_path") && args["output_path"].is_string()) {
		args_.output_path = args["output_path"].get<std::string>();
	} else {
		args_.output_path.clear();
	}

	if (args.contains("async") && args["async"].is_boolean()) {
		args_.is_async = args["async"].get<bool>();
	} else {
		args_.is_async = false;
	}

	if (args_.path.find("://") == std::string::npos) {
		std::string canonical_path;
		if (!ctx.fs_security.validate_access(args_.path, agentlib::access_type::read, canonical_path, out_error)) {
			return false;
		}
		args_.safe_path = canonical_path;
	} else {
		args_.safe_path = args_.path;
	}

	if (!args_.output_path.empty()) {
		if (args_.output_path.find("://") == std::string::npos) {
			std::string canonical_output;
			if (!ctx.fs_security.validate_access(args_.output_path, agentlib::access_type::write, canonical_output, out_error)) {
				return false;
			}
			args_.safe_output_path = canonical_output;
		} else {
			args_.safe_output_path = args_.output_path;
		}
	}

	return true;
}

std::unique_ptr<agentlib::llm_tool> markdown_extract_validator::create_tool_impl(const nlohmann::json & /*args*/) const
{
	return std::make_unique<markdown_extract_tool>(args_);
}

std::vector<agentlib::tool_example> markdown_extract_validator::get_examples() const {
	return {
		{
			"Targeted Directive Extraction from VFS Manpage",
			nlohmann::json{{"path", "system://man/systemd.exec.md"}, {"query", "ProtectKernelTunables"}},
			"Full Flow: 1) Call fs_man(name='systemd.exec') to render system://man/systemd.exec.md -> 2) Call markdown_extract(path='system://man/systemd.exec.md', query='ProtectKernelTunables') to extract targeted directive section without reading thousands of lines."
		},
		{
			"Section Heading Extraction from Workspace Document",
			nlohmann::json{{"path", "docs/design.md"}, {"query", "Architecture"}},
			"Extracts targeted Architecture section from design documentation without reading full file."
		}
	};
}



} // namespace tools


extern "C" {

void register_markdown_extract(void)
{
	agentlib::tool_registry::get_instance().register_validator(
	    []() { return std::make_unique<tools::markdown_extract_validator>(); });
}

void unregister_markdown_extract(void)
{
	agentlib::tool_registry::get_instance().unregister_validator("markdown_extract");
}

}
