#pragma once

#include <mutex>
#include <queue>
#include <optional>
#include <string>

/**
 * @brief Event types supported by the editor.
 */
enum class event_type {
	key_press,  ///< A keyboard input event
	quit,       ///< An application exit event
	load,       ///< Request to load a file (triggers dialog)
	save        ///< Request to save a file (triggers dialog)
};

/**
 * @brief Represents a single event in the editor system.
 * 
 * The event model follows a two-tier dispatch system:
 * 1. Global Queue: All raw input events (keys, system events) are pushed here.
 * 2. Central Dispatcher: Processes the Global Queue and routes events to the 
 *    focused component's local queue or handler.
 * 3. Local/Window Queue: Components (like windows) have their own queues for 
 *    asynchronous processing of routed events.
 */
struct editor_event {
	event_type type;
	int key_code{0};  ///< NCurses key code or ASCII value
	std::string utf8_char; ///< UTF-8 character sequence for typing
};

/**
 * @brief Thread-safe event queue for passing messages between components and threads.
 */
class event_queue {
public:
	event_queue() = default;
	~event_queue() = default;

	void push(const editor_event& ev);
	std::optional<editor_event> pop();

private:
	std::queue<editor_event> queue_;
	mutable std::mutex mutex_;
};
