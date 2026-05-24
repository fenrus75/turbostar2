#include <algorithm>
#include <chrono>
#include <fstream>
#include <lsp/json/json.h>
#include <ncurses.h>
#include <sstream>
#include "build_error_manager.h"
#include "config_manager.h"
#include "editor.h"
#include "event_logger.h"
#include "fs_utils.h"
#include "gcc_log_parser.h"
#include "git_manager.h"
#include "history_manager.h"
#include "lsp_manager.h"

namespace fs = std::filesystem;

void editor::dispatch_event_git(const editor_event &ev)
{
	auto &logger = event_logger::get_instance();

	if (ev.type == event_type::git_status_updated) {
		logger.log("Dispatching git_status_updated event.");
		for (auto &doc : documents_) {
			git_info info = git_manager::get_instance().get_cached_info(doc->get_filename());
			doc->set_git_branch(info.branch);
		}
		for (auto &win : windows_) {
			win->invalidate();
		}
		return;
	}

	if (ev.type == event_type::git_add) {
		logger.log("Dispatching git_add event.");
		std::shared_ptr<document> doc = get_active_doc();
		if (doc && !doc->get_filename().empty()) {
			git_manager::get_instance().git_add(doc->get_filename());
		}
		return;
	}

	if (ev.type == event_type::git_refresh) {
		logger.log("Dispatching git_refresh event.");
		std::shared_ptr<document> doc = get_active_doc();
		if (doc && !doc->get_filename().empty()) {
			git_manager::get_instance().request_status(doc->get_filename());
		}
		return;
	}
}
