#include "editor.h"
#include <algorithm>
#include <chrono>
#include <ncurses.h>
#include "event_logger.h"
#include "ui/file_dialog.h"
#include "ui/find_dialog.h"
#include "history_manager.h"
#include "config_manager.h"
#include "git_manager.h"
#include "lsp_manager.h"
#include "gcc_log_parser.h"
#include "build_error_manager.h"
#include "fs_utils.h"
#include <fstream>
#include <sstream>
#include <lsp/json/json.h>

namespace fs = std::filesystem;

void editor::dispatch_event_search(const editor_event &ev)
{
	auto &logger = event_logger::get_instance();

	if (ev.type == event_type::find) {
		logger.log("Dispatching find event (advanced dialog).");
		active_dialog_ = create_search_dialog("Find", current_search_, false);
		active_dialog_mode_ = dialog_mode::search;
		set_focus(focus_target::dialog, "menu_find");
		return;
	}

	if (ev.type == event_type::replace) {
		logger.log("Dispatching replace event (advanced dialog).");
		active_dialog_ = create_search_dialog("Replace", current_search_, true);
		active_dialog_mode_ = dialog_mode::replace;
		set_focus(focus_target::dialog, "menu_replace");
		return;
	}

}
