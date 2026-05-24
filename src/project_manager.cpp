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

        lsp_manager_ = std::make_unique<lsp_manager>();

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

void project_manager::lsp_start(event_queue &queue) { if (lsp_manager_) lsp_manager_->start(queue); }
void project_manager::lsp_stop() { if (lsp_manager_) lsp_manager_->stop(); }
void project_manager::lsp_open_document(const std::string &filepath, const std::string &text) { if (lsp_manager_) lsp_manager_->open_document(filepath, text); }
void project_manager::lsp_update_document(const std::string &filepath, const std::string &text) { if (lsp_manager_) lsp_manager_->update_document(filepath, text); }
void project_manager::lsp_request_hover(const std::string &filepath, int line, int character) { if (lsp_manager_) lsp_manager_->request_hover(filepath, line, character); }
void project_manager::lsp_request_document_highlight(const std::string &filepath, int line, int character) { if (lsp_manager_) lsp_manager_->request_document_highlight(filepath, line, character); }
void project_manager::lsp_request_selection_range(const std::string &filepath, int line, int character) { if (lsp_manager_) lsp_manager_->request_selection_range(filepath, line, character); }
bool project_manager::lsp_is_supported_file(const std::string &filepath) const { if (lsp_manager_) return lsp_manager_->is_supported_file(filepath); return false; }

std::vector<text_range> project_manager::lsp_query_selection_ranges(const std::string &filepath, int line, int character)
{
	if (lsp_manager_) return lsp_manager_->query_selection_ranges(filepath, line, character);
	return {};
}

std::vector<lsp_manager::location_info> project_manager::lsp_query_definition(const std::string &filepath, int line, int character)
{
	if (lsp_manager_) return lsp_manager_->query_definition(filepath, line, character);
	return {};
}

std::vector<lsp_manager::location_info> project_manager::lsp_query_references(const std::string &filepath, int line, int character)
{
	if (lsp_manager_) return lsp_manager_->query_references(filepath, line, character);
	return {};
}
