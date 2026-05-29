#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include "config_manager.h"
#include "document.h"
#include "event_logger.h"
#include "fs_utils.h"
#include "git_manager.h"
#include "highlighter_registry.h"
#include "lsp_manager.h"

namespace fs = std::filesystem;

void document::mark_line_dirty(const std::shared_ptr<line> &l)
{
	std::lock_guard<std::mutex> lock(dirty_mutex_);
	dirty_lines_.push(l);
	dirty_cv_.notify_one();
}

void document::highlighter_thread_loop(std::stop_token stop_token)
{
	event_logger::get_instance().log(std::format("Thread started: document highlighter_thread_loop ({})", filename_));
	while (!stop_token.stop_requested()) {
		std::shared_ptr<line> l;
		{
			std::unique_lock<std::mutex> lock(dirty_mutex_);
			dirty_cv_.wait(lock, [&] { return !dirty_lines_.empty() || stop_token.stop_requested(); });
			if (stop_token.stop_requested())
				break;
			l = dirty_lines_.front();
			dirty_lines_.pop();
		}
		if (l) {
			process_line_highlight(l);

			// If no more lines in queue, request a redraw
			{
				std::lock_guard<std::mutex> lock(dirty_mutex_);
				if (dirty_lines_.empty()) {
					editor_event ev;
					ev.type = event_type::redraw;
					global_queue_.push(ev);
				}
			}
		}
	}
	event_logger::get_instance().log(std::format("Thread exited: document highlighter_thread_loop ({})", filename_));
}

void document::refresh_highlighter()
{
	active_highlighter_ = highlighter_registry::get_instance().get_highlighter_for_file(filename_);
}

void document::process_line_highlight(std::shared_ptr<line> l)
{
	std::shared_ptr<syntax_highlighter> highlighter;
	{
		std::shared_lock lock(mutex_);
		highlighter = active_highlighter_;
	}

	if (highlighter) {
		try {
			highlighter->highlight(l);
		} catch (const std::exception &e) {
			event_logger::get_instance().log(std::format("Highlighter error: {}", e.what()));
		} catch (...) {
			event_logger::get_instance().log("Highlighter error: unknown exception");
		}
	}
}

