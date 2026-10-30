// Tested source file: src/rendering_thread_guard.cpp
#include "test_watchdog.h"
#include <cassert>
#include <thread>
#include <string>
#include "../../src/rendering_thread_guard.h"
#include "../../src/event_logger.h"

int main()
{
	test_watchdog::setup_watchdog(30);
	auto &logger = event_logger::get_instance();
	logger.clear_all();

	// 1. Initially, outside of any guard, current thread is not the rendering thread.
	assert(!rendering_thread_guard::is_rendering_thread());

	// warn_high_latency on non-rendering thread should be a complete no-op and NOT log.
	rendering_thread_guard::warn_high_latency("test_tag", "background task taking {} ms", 100);
	assert(!logger.get_latest_matching_message("test_tag").has_value());

	// 2. Inside rendering_thread_guard scope, current thread IS the rendering thread.
	{
		rendering_thread_guard guard;
		assert(rendering_thread_guard::is_rendering_thread());

		// warn_high_latency on rendering thread should log a warning
		rendering_thread_guard::warn_high_latency("test_tag", "blocking task on UI thread: {}", "step 1");
		auto match = logger.get_latest_matching_message("blocking task on UI thread");
		assert(match.has_value());
		assert(match->find("[WARN]") != std::string::npos);
		assert(match->find("[test_tag]") != std::string::npos);
		assert(match->find("step 1") != std::string::npos);

		// Test overload with raw string_view
		rendering_thread_guard::warn_high_latency("another_tag", "direct message warning");
		auto match2 = logger.get_latest_matching_message("direct message warning");
		assert(match2.has_value());
		assert(match2->find("[WARN]") != std::string::npos);
		assert(match2->find("[another_tag]") != std::string::npos);

		// Nested background thread should NOT inherit rendering thread flag
		std::thread bg_thread([&]() {
			assert(!rendering_thread_guard::is_rendering_thread());
			rendering_thread_guard::warn_high_latency("bg_tag", "this should not be logged");
		});
		bg_thread.join();
		assert(!logger.get_latest_matching_message("bg_tag").has_value());
	}

	// 3. After guard destruction, thread is no longer the rendering thread
	assert(!rendering_thread_guard::is_rendering_thread());
	rendering_thread_guard::warn_high_latency("post_scope_tag", "should not log");
	assert(!logger.get_latest_matching_message("post_scope_tag").has_value());

	return 0;
}
