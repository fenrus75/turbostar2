#pragma once

#include <format>
#include <string>
#include <string_view>
#include <utility>

/**
 * @brief Thread-local RAII guard marking the UI/rendering thread.
 *
 * Used to detect and warn when synchronous, high-latency operations (such as disk I/O,
 * subprocess executions, heavy language server queries) are accidentally executed
 * on the main rendering thread, causing UI frame drops or latency spikes.
 */
class rendering_thread_guard
{
      public:
	rendering_thread_guard() noexcept;
	~rendering_thread_guard() noexcept;

	// Non-copyable, non-movable RAII guard
	rendering_thread_guard(const rendering_thread_guard &) = delete;
	rendering_thread_guard &operator=(const rendering_thread_guard &) = delete;
	rendering_thread_guard(rendering_thread_guard &&) = delete;
	rendering_thread_guard &operator=(rendering_thread_guard &&) = delete;

	/**
	 * @brief Returns true if the calling thread is currently marked as the rendering thread.
	 */
	[[nodiscard]] static bool is_rendering_thread() noexcept;

	/**
	 * @brief Emits a warning to event_logger if called on the rendering thread.
	 *
	 * Fast inline check on background threads (single TLS read).
	 *
	 * @param tag Unique identifier for easy grep-ability in the codebase (e.g. "command_runner").
	 * @param fmt Format string describing the operation.
	 * @param args Format arguments.
	 */
	template <typename... Args>
	static void warn_high_latency(std::string_view tag, std::format_string<Args...> fmt, Args &&...args) noexcept
	{
		if (is_rendering_thread()) {
			std::string details = std::format(fmt, std::forward<Args>(args)...);
			log_latency_warning(tag, details);
		}
	}

	/**
	 * @brief Overload taking a simple string message with tag.
	 */
	static void warn_high_latency(std::string_view tag, std::string_view message) noexcept
	{
		if (is_rendering_thread()) {
			log_latency_warning(tag, message);
		}
	}

      private:
	static void log_latency_warning(std::string_view tag, std::string_view details) noexcept;

	inline static thread_local bool is_rendering_thread_{false};
};
