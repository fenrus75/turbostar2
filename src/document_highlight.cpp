#include "document.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <regex>
#include "event_logger.h"
#include "config_manager.h"
#include "git_manager.h"
#include "highlighter_registry.h"
#include "clangd_manager.h"
#include "fs_utils.h"

namespace fs = std::filesystem;


void document::mark_line_dirty(std::shared_ptr<line> l)
{
	std::lock_guard lock(dirty_mutex_);
	dirty_lines_.push(l);
	dirty_cv_.notify_one();
}


void document::highlighter_thread_loop()
{
	while (!stop_thread_) {
		std::shared_ptr<line> l;
		{
			std::unique_lock lock(dirty_mutex_);
			dirty_cv_.wait(lock, [&] { return !dirty_lines_.empty() || stop_thread_; });
			if (stop_thread_)
				break;
			l = dirty_lines_.front();
			dirty_lines_.pop();
		}
		if (l) {
			process_line_highlight(l);

			// If no more lines in queue, request a redraw
			{
				std::lock_guard lock(dirty_mutex_);
				if (dirty_lines_.empty()) {
					editor_event ev;
					ev.type = event_type::redraw;
					global_queue_.push(ev);
				}
			}
		}
	}
}


void document::refresh_highlighter()
{
	active_highlighter_ = highlighter_registry::get_instance().get_highlighter_for_file(filename_);
}


void document::process_line_highlight(std::shared_ptr<line> l)
{
	if (active_highlighter_) {
		active_highlighter_->highlight(l);
	}
}
