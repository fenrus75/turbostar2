#include <nlohmann/json.hpp>
#include <string>
#include <re2/re2.h>
#include "agentlib/tool_registry.h"
#include "agentlib/tool_validator.h"
#include "fs_utils.h"
#include "elf_list_symbols.h"

namespace tools
{

struct elf_list_symbols_raw_args {
	std::string path;
	std::string pattern = "";
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(elf_list_symbols_raw_args, path, pattern);

class elf_list_symbols_validator : public agentlib::tool_validator
{
      public:
	bool is_pure() const override
	{
		return true;
	}

	std::string get_name() const override
	{
		return "elf_list_symbols";
	}

	std::string get_description() const override
	{
		return "Lists all symbols in the symbol table of an ELF file, providing their name, offset/value, and size. "
		       "Allows optional case-insensitive substring or regex filtering via the 'pattern' argument.";
	}

	std::string get_family() const override
	{
		return "x86";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {
			{"type", "object"},
			{"properties", {
				{"path", {{"type", "string"}, {"description", "The path to the ELF file relative to project root."}}},
				{"pattern", {{"type", "string"}, {"description", "Optional substring or regex pattern to filter symbol names."}}}
			}},
			{"required", nlohmann::json::array({"path"})}
		};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx,
				std::string &out_error) const override
	{
		try {
			elf_list_symbols_raw_args parsed = raw_json.get<elf_list_symbols_raw_args>();
			if (parsed.path.empty()) {
				out_error = "Path parameter cannot be empty.";
				return false;
			}

			std::string canonical_path;
			if (!ctx.fs_security.validate_access(parsed.path, agentlib::access_type::read, canonical_path, out_error)) {
				return false;
			}

			if (!parsed.pattern.empty()) {
				if (parsed.pattern.length() > 256) {
					out_error = "Validation Error: pattern exceeds maximum length of 256 characters.";
					return false;
				}
				if (!fs_utils::is_safe_for_ui(parsed.pattern)) {
					out_error = "Security Violation: pattern contains unsafe control characters.";
					return false;
				}
				re2::RE2::Options options;
				options.set_case_sensitive(false);
				re2::RE2 test_re(parsed.pattern, options);
				if (!test_re.ok()) {
					out_error = "Validation Error: Invalid regular expression pattern.";
					return false;
				}
			}

			args_.requested_path = parsed.path;
			args_.safe_path = canonical_path;
			args_.pattern = parsed.pattern;

			return true;
		} catch (const std::exception &e) {
			out_error = "Invalid arguments: " + std::string(e.what());
			return false;
		}
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json & /*raw_json*/) const override
	{
		return std::make_unique<elf_list_symbols_tool>(args_);
	}

      private:
	mutable elf_list_symbols_args args_;
};

} // namespace tools

extern "C" {
void register_elf_list_symbols(void)
{
	agentlib::tool_registry::get_instance().register_validator([]() { return std::make_unique<tools::elf_list_symbols_validator>(); });
}

void unregister_elf_list_symbols(void)
{
	agentlib::tool_registry::get_instance().unregister_validator("elf_list_symbols");
}
}
