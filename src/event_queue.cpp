#include "event_queue.h"
#include "event_logger.h"

void event_queue::push(const editor_event &ev)
{
	std::lock_guard<std::mutex> lock(mutex_);
	queue_.push(ev);
}

std::optional<editor_event> event_queue::pop()
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (queue_.empty()) {
		return std::nullopt;
	}
	editor_event ev = queue_.front();
	queue_.pop();
	return ev;
}
