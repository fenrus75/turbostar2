#include "plugins/html/html_extract_tables_validator.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include "agentlib/tool_registry.h"

namespace tools
{

struct html_extract_tables_raw_args {
	std::string path;
	std::string output_path;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(html_extract_tables_raw_args, path, output_path);

bool html_extract_tables_validator::validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx,
						       std::string &out_error) const
{
	try {
		html_extract_tables_raw_args parsed = raw_json.get<html_extract_tables_raw_args>();
		if (parsed.path.empty()) {
			out_error = "Path cannot be empty.";
			return false;
		}

		std::string canonical_path;
		if (!ctx.fs_security.validate_access(parsed.path, agentlib::access_type::read, canonical_path, out_error)) {
			return false;
		}

		// Enforce 5MB size limit
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

		std::string canonical_output_path;
		if (!parsed.output_path.empty()) {
			if (!ctx.fs_security.validate_access(parsed.output_path, agentlib::access_type::write, canonical_output_path, out_error)) {
				return false;
			}
		}

		args_.requested_path = parsed.path;
		args_.safe_path = canonical_path;
		args_.output_path = parsed.output_path;
		args_.safe_output_path = canonical_output_path;

		return true;
	} catch (const std::exception &e) {
		out_error = "Invalid arguments: " + std::string(e.what());
		return false;
	}
}

std::unique_ptr<agentlib::llm_tool> html_extract_tables_validator::create_tool_impl(const nlohmann::json & /*raw_json*/) const
{
	return std::make_unique<html_extract_tables_tool>(args_);
}

} // namespace tools

extern "C" {
void register_html_extract_tables(void)
{
	agentlib::tool_registry::get_instance().register_validator([]() { return std::make_unique<tools::html_extract_tables_validator>(); });
}

void unregister_html_extract_tables(void)
{
	agentlib::tool_registry::get_instance().unregister_validator("html_extract_tables");
}
}
