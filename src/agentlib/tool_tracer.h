#pragma once

#include <atomic>
#include <mutex>
#include <string>

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
	 */
	void trace_tool_call(const std::string &tool_name, const std::string &args_json_string, const std::string &output_result);

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
