#include <nlohmann/json.hpp>
#include <string>
#include "../../agentlib/tool_registry.h"
#include "../../agentlib/tool_validator.h"
#include "hex_inspect_range.h"

namespace tools
{

struct hex_inspect_range_raw_args {
	std::string path;
	int start_offset = 0;
	int size = 256;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(hex_inspect_range_raw_args, path, start_offset, size);

class hex_inspect_range_validator : public agentlib::tool_validator
{
      public:
	bool is_pure() const override
	{
		return true;
	}

	std::string get_name() const override
	{
		return "hex_inspect_range";
	}

	std::string get_description() const override
	{
		return "Inspects the semantic/structural details of a binary file range using registered syntax highlighters (like ELF or PNG). "
		       "Returns a markdown list of fields, offsets, sizes, and annotations in the range.";
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
				{"path", {{"type", "string"}, {"description", "The path to the binary file relative to project root."}}},
				{"start_offset", {{"type", "integer"}, {"description", "0-based byte offset to start inspecting. Defaults to 0."}}},
				{"size", {{"type", "integer"}, {"description", "Number of bytes to inspect. Defaults to 256. Maximum 4096."}}}
			}},
			{"required", nlohmann::json::array({"path"})}
		};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx,
				std::string &out_error) const override
	{
		try {
			hex_inspect_range_raw_args parsed = raw_json.get<hex_inspect_range_raw_args>();
			if (parsed.path.empty()) {
				out_error = "Path parameter cannot be empty.";
				return false;
			}

			std::string canonical_path;
			if (!ctx.fs_security.validate_access(parsed.path, agentlib::access_type::read, canonical_path, out_error)) {
				return false;
			}

			args_.requested_path = parsed.path;
			args_.safe_path = canonical_path;
			args_.start_offset = (parsed.start_offset < 0) ? 0 : parsed.start_offset;
			args_.size = (parsed.size <= 0) ? 256 : (parsed.size > 4096 ? 4096 : parsed.size);

			return true;
		} catch (const std::exception &e) {
			out_error = "Invalid arguments: " + std::string(e.what());
			return false;
		}
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json & /*raw_json*/) const override
	{
		return std::make_unique<hex_inspect_range_tool>(args_);
	}

      private:
	mutable hex_inspect_range_args args_;
};

REGISTER_TOOL(hex_inspect_range_validator)

} // namespace tools
