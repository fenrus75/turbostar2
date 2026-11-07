#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

namespace agentlib {

/**
 * @class tool_tracer
 * @brief Thread-safe tracer for recording tool invocations and outputs to sequential files.
 *
 * Log files are written as toolcall.0, toolcall.1, toolcall.2... in the current working directory.
 */
class tool_tracer {
      public:
	static tool_tracer &get_instance();

	void set_enabled(bool enabled) noexcept;
	bool is_enabled() const noexcept;
	void reset() noexcept;

	/**
	 * @brief Traces a single tool call to toolcall.N in the current working directory.
	 * @param tool_name The name of the executed tool.
	 * @param args_json_string The JSON parameters requested by the LLM.
	 * @param output_result The exact output string returned to the LLM.
	 * @param prepare_duration_us Microseconds spent in prepare_tool (Stage 1 & Stage 2 validation).
	 * @param execute_duration_us Microseconds spent executing the tool.
	 */
	void trace_tool_call(std::string_view tool_name, std::string_view args_json_string, std::string_view output_result,
			     uint64_t prepare_duration_us = 0, uint64_t execute_duration_us = 0);

      private:
	tool_tracer() = default;

	bool enabled_{false};
	std::atomic<size_t> counter_{0};

	/*
	 * mutex_ protects file I/O operations and file creation in the current directory
	 * when tracing tool calls from concurrent background subagents or thread pools.
	 * Locking guidelines: Acquire mutex_ during trace_tool_call before writing to disk.
	 */
	mutable std::mutex mutex_;
};

} // namespace agentlib
