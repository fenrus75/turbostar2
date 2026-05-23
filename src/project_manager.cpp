#include "project_manager.h"
#include "git_manager.h"
#include "event_logger.h"
#include "crashdump_manager.h"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

project_manager &project_manager::get_instance()
{
        static project_manager instance;
        return instance;
}

void project_manager::initialize()
{
        repo_root_ = git_manager::get_instance().get_repository_root();
        if (repo_root_.empty()) {
                repo_root_ = fs::current_path().string();
        }

        // Clean up previous runs' crash dumps on startup
        crashdump_manager::get_instance().clear_all();

        load_instructions();
}
void project_manager::load_instructions()
{
	if (repo_root_.empty()) return;

	fs::path root(repo_root_);
	fs::path agent_md = root / "AGENT.md";
	fs::path gemini_md = root / "GEMINI.md";

	fs::path target;
	if (fs::exists(agent_md)) {
		target = agent_md;
	} else if (fs::exists(gemini_md)) {
		target = gemini_md;
	}

	if (!target.empty()) {
		std::ifstream file(target);
		if (file.is_open()) {
			std::stringstream ss;
			ss << file.rdbuf();
			instructions_ = ss.str();
			event_logger::get_instance().log("Loaded project instructions from " + target.string());
		}
	}
}
