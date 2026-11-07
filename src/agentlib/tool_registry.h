#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string_view>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <iostream>
#include "tool_context.h"
#include "tool_validator.h"
#include "agent_properties.h"

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
	~tool_registry();

	// Returns the JSON array of tools to inject into the OpenAI payload
	nlohmann::json get_tools_json(bool mutation_possible = true, const agent_properties &properties = {}) const;

	std::vector<std::shared_ptr<tool_validator>> get_active_tools(
		bool mutation_possible = true,
		const agent_properties &properties = {}
	) const;

	std::vector<std::string> get_all_registered_families() const;

	std::vector<std::shared_ptr<tool_validator>> get_all_registered_validators() const;

	std::vector<tool_example> get_tool_examples(const std::string &name) const;

	// Checks if the tool should be silent in the UI by default
	bool is_tool_silent(const std::string &name) const;


	struct tool_preparation_result {
		std::unique_ptr<llm_tool> tool;
		std::string error_message; // If non-empty, preparation failed
		uint64_t prepare_duration_us{0};
	};

	// Performs parsing, Stage 1, and Stage 2 validation without executing
	tool_preparation_result prepare_tool(const std::string &name, const std::string &args_json_string, tool_context &ctx) const;

	// Executes a prepared tool instance with central document saving, statistics, exception handling, and tracing
	std::string execute_prepared_tool(const std::string &name, const std::string &args_json_string, const tool_preparation_result &prep, tool_context &ctx) const;

	// Executes the two-stage security and execution pipeline (Legacy / Convenience)
	std::string execute_tool(const std::string &name, const std::string &args_json_string, tool_context &ctx) const;

	// Executes the tool pipeline and unwraps any XML prompt data tag (<tag>...</tag>)
	std::string execute_tool_unwrapped(const std::string &name, const std::string &args_json_string, tool_context &ctx) const;

	// Used to register and query activation reasons for tool families
	void register_tool_family(const std::string &name, const std::string &reason, const std::string &guidance = "");
	void unregister_tool_family(const std::string &name);
	std::string get_tool_family_reason(const std::string &name) const;
	std::string get_tool_family_guidance(const std::string &name) const;
	bool has_tool_family(const std::string &name) const;

	struct tool_timing_stats {
		uint64_t calls{0};
		uint64_t errors{0};
		uint64_t total_prepare_us{0};
		uint64_t total_execute_us{0};
		uint64_t max_total_us{0};
		uint64_t last_prepare_us{0};
		uint64_t last_execute_us{0};
		bool last_failed{false};
		std::string last_args_json;
		uint64_t last_cache_hits_delta{0};
		uint64_t last_cache_misses_delta{0};
		uint64_t last_cache_evictions_delta{0};
		uint64_t last_cache_prefetches_delta{0};
	};

	// Records and retrieves in-memory per-tool timing statistics for benchmarking and replay analysis
	void record_tool_timing(std::string_view name, std::string_view args_json, uint64_t prepare_us, uint64_t execute_us,
				bool failed = false, uint64_t hits_delta = 0, uint64_t misses_delta = 0,
				uint64_t evictions_delta = 0, uint64_t prefetches_delta = 0) const;
	std::map<std::string, tool_timing_stats> get_timing_stats() const;
	void reset_timing_stats() const;

      private:
	tool_registry();
	std::map<std::string, validator_factory> validator_factories_;
	std::map<std::string, std::string> family_reasons_;
	std::map<std::string, std::string> family_guidances_;
	mutable std::map<std::string, tool_timing_stats> timing_stats_;

	/*
	 * timing_mutex_ protects the timing_stats_ map across concurrent tool executions.
	 * Locking guidelines: Acquire briefly at the end of execute_prepared_tool or when reading/resetting stats.
	 */
	mutable std::mutex timing_mutex_;

	/*
	 * mutex_ protects the validator_factories_ map, family_reasons_ map, and family_guidances_ map.
	 *
	 * Locking Rules:
	 * - Held briefly during tool/family registration, unregistration, tool list querying,
	 *   and tool preparation lookup.
	 * - Declared as recursive_mutex because some validator descriptions (such as
	 *   activate_tool_family_validator::get_description) query the registry back
	 *   recursively during tools serialization.
	 */
	mutable std::recursive_mutex mutex_;
};

// Helper macro for static self-registration
// Usage: REGISTER_TOOL(my_validator_class)
template <typename T> struct tool_registrar {
	std::string name;
	tool_registrar()
	{
		name = T().get_name();
		tool_registry::get_instance().register_validator([]() { return std::make_unique<T>(); });
	}
	~tool_registrar()
	{
		tool_registry::get_instance().unregister_validator(name);
	}
};

#define REGISTER_TOOL(ValidatorClass) static agentlib::tool_registrar<ValidatorClass> global_##ValidatorClass##_registrar;

} // namespace agentlib


#include <optional>
#include <nlohmann/json.hpp>

namespace nlohmann {
    template <typename T>
    struct adl_serializer<std::optional<T>> {
        static void to_json(json& j, const std::optional<T>& opt) {
            if (!opt) {
                j = nullptr;
            } else {
                j = *opt; // Recursively triggers underlying type serialization
            }
        }

        static void from_json(const json& j, std::optional<T>& opt) {
            if (j.is_null()) {
                opt = std::nullopt;
            } else {
                opt = j.get<T>(); // Recursively triggers underlying type deserialization
            }
        }
    };
}
