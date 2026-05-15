#pragma once

#include <mutex>
#include <queue>
#include <optional>

enum class event_type {
	key_press,
	quit
};

struct editor_event {
	event_type type;
	int key_code{0};
};

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
