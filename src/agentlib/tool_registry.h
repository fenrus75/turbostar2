#pragma once
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include "tool_context.h"
#include "tool_validator.h"

namespace agentlib
{

class tool_registry
{
      public:
	using validator_factory = std::function<std::unique_ptr<tool_validator>()>;

	static tool_registry &get_instance();

	// Used by self-registering tools
	void register_validator(validator_factory factory);
	void unregister_validator(const std::string &name);

	// Returns the JSON array of tools to inject into the OpenAI payload
	nlohmann::json get_tools_json(const std::vector<std::string> &active_families = {}) const;

	// Returns the JSON array of tools to inject into the Gemini payload
	nlohmann::json get_gemini_tools_json(const std::vector<std::string> &active_families = {}) const;

	std::vector<std::string> get_all_registered_families() const;

	// Checks if the tool should be silent in the UI by default
	bool is_tool_silent(const std::string &name) const;

	struct tool_preparation_result {
		std::unique_ptr<llm_tool> tool;
		std::string error_message; // If non-empty, preparation failed
	};

	// Performs parsing, Stage 1, and Stage 2 validation without executing
	tool_preparation_result prepare_tool(const std::string &name, const std::string &args_json_string, tool_context &ctx) const;

	// Executes the two-stage security and execution pipeline (Legacy / Convenience)
	std::string execute_tool(const std::string &name, const std::string &args_json_string, tool_context &ctx) const;

      private:
	tool_registry() = default;
	std::map<std::string, validator_factory> validator_factories_;
	mutable std::recursive_mutex mutex_;
};

// Helper macro for static self-registration
// Usage: REGISTER_TOOL(my_validator_class)
template <typename T> struct tool_registrar {
	tool_registrar()
	{
		tool_registry::get_instance().register_validator([]() { return std::make_unique<T>(); });
	}
};

#define REGISTER_TOOL(ValidatorClass) static agentlib::tool_registrar<ValidatorClass> global_##ValidatorClass##_registrar;

} // namespace agentlib
