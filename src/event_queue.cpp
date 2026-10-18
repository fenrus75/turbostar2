#include "event_queue.h"
#include "event_logger.h"

void event_queue::push(const editor_event &ev)
{
	std::lock_guard<std::mutex> lock(mutex_);
	queue_.push(ev);
}

void event_queue::push(editor_event &&ev)
{
	std::lock_guard<std::mutex> lock(mutex_);
	queue_.push(std::move(ev));
}

void event_queue::push(event_type type)
{
	push(editor_event{.type = type});
}

void event_queue::push(event_type type, int key_code)
{
	push(editor_event{.type = type, .key_code = key_code});
}

void event_queue::push(event_type type, std::string_view payload)
{
	push(editor_event{.type = type, .payload = std::string(payload)});
}

void event_queue::push(event_type type, int key_code, std::string_view payload)
{
	push(editor_event{.type = type, .key_code = key_code, .payload = std::string(payload)});
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
