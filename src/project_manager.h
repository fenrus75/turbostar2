#pragma once
#include <string>
#include <memory>
#include "lsp_manager.h"

/**
 * @brief Manages project-level context, such as repository root and project-specific instructions (AGENT.md/GEMINI.md).
 */
class project_manager
{
      public:
	static project_manager &get_instance();

	/**
	 * @brief Initializes the project manager by locating the repository root and loading instructions.
	 */
	void initialize();

	/**
	 * @brief Returns the absolute path to the project/repository root.
	 */
	std::string get_repository_root() const { return repo_root_; }

	/**
	 * @brief Returns the content of AGENT.md or GEMINI.md if found at the root.
	 */
	std::string get_project_instructions() const { return instructions_; }

	// LSP delegation methods
	void lsp_start(event_queue &queue);
	void lsp_stop();
	void lsp_open_document(const std::string &filepath, const std::string &text);
	void lsp_update_document(const std::string &filepath, const std::string &text);
	void lsp_request_hover(const std::string &filepath, int line, int character);
	void lsp_request_document_highlight(const std::string &filepath, int line, int character);
	void lsp_request_selection_range(const std::string &filepath, int line, int character);
	bool lsp_is_supported_file(const std::string &filepath) const;

	// Synchronous LSP queries
	std::vector<text_range> lsp_query_selection_ranges(const std::string &filepath, int line, int character);
	std::vector<lsp_manager::location_info> lsp_query_definition(const std::string &filepath, int line, int character);
	std::vector<lsp_manager::location_info> lsp_query_references(const std::string &filepath, int line, int character);

      private:
	project_manager() = default;
	
	void load_instructions();

	std::string repo_root_;
	std::string instructions_;
	std::unique_ptr<lsp_manager> lsp_manager_;
};
