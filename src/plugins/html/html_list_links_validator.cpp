#include "plugins/html/html_list_links_validator.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include "agentlib/tool_registry.h"
#include "fs_utils.h"

namespace tools
{

struct html_list_links_raw_args {
	std::string path;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(html_list_links_raw_args, path);

bool html_list_links_validator::validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx,
						   std::string &out_error) const
{
	try {
		html_list_links_raw_args parsed = raw_json.get<html_list_links_raw_args>();
		if (parsed.path.empty()) {
			out_error = "Path cannot be empty.";
			return false;
		}

		std::string canonical_path;
		if (!ctx.fs_security.validate_access(parsed.path, agentlib::access_type::read, canonical_path, out_error)) {
			return false;
		}

		// Enforce 5MB size limit and regular file check
		if (!fs_utils::is_regular_file(canonical_path)) {
			out_error = "Target path is not a regular file.";
			return false;
		}
		std::error_code ec;
		auto sz = std::filesystem::file_size(canonical_path, ec);
		if (ec) {
			out_error = "Failed to access target file: " + ec.message();
			return false;
		}
		if (sz > 5 * 1024 * 1024) {
			out_error = "File exceeds the 5MB size limit (got " + std::to_string(sz) + " bytes).";
			return false;
		}

		args_.requested_path = parsed.path;
		args_.safe_path = canonical_path;

		return true;
	} catch (const std::exception &e) {
		out_error = "Invalid arguments: " + std::string(e.what());
		return false;
	}
}

std::unique_ptr<agentlib::llm_tool> html_list_links_validator::create_tool_impl(const nlohmann::json & /*raw_json*/) const
{
	return std::make_unique<html_list_links_tool>(args_);
}

} // namespace tools

extern "C" {
void register_html_list_links(void)
{
	agentlib::tool_registry::get_instance().register_validator([]() { return std::make_unique<tools::html_list_links_validator>(); });
}

void unregister_html_list_links(void)
{
	agentlib::tool_registry::get_instance().unregister_validator("html_list_links");
}
}
