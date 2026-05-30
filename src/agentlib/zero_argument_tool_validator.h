#pragma once
#include <string>
#include <memory>
#include <nlohmann/json.hpp>
#include "tool_validator.h"

namespace agentlib {

// A specialized base class for tools that take exactly zero arguments.
// Hides all JSON parsing and schema generation from the derived tool,
// and enforces that no unexpected arguments are passed.
class zero_argument_tool_validator : public tool_validator {
public:
	virtual ~zero_argument_tool_validator() = default;

	// Required overrides for the derived class
	virtual std::string get_name() const override = 0;
	virtual std::string get_description() const override = 0;

	// --- Final implementations of the base tool_validator interface ---

	nlohmann::json get_parameters_schema() const final {
		return {
			{"type", "object"},
			{"properties", nlohmann::json::object()},
			{"additionalProperties", false}
		};
	}

protected:
	bool validate_args_impl(const nlohmann::json& raw_args, const tool_context& ctx, std::string& out_error) const final {
		if (raw_args.is_object() && !raw_args.empty()) {
			out_error = "Tool '" + get_name() + "' expects no arguments, but received some.";
			return false;
		}
		return validate_zero_args(ctx, out_error);
	}

	std::unique_ptr<llm_tool> create_tool_impl(const nlohmann::json& /*raw_args*/) const final {
		return create_tool_from_zero_args();
	}

	// New hooks for the end-user tool
	virtual bool validate_zero_args(const tool_context& /*ctx*/, std::string& /*out_error*/) const {
		return true;
	}
	virtual std::unique_ptr<llm_tool> create_tool_from_zero_args() const = 0;
};

} // namespace agentlib
