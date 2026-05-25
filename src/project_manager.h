#pragma once
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "lsp_manager.h"

/**
 * @brief Manages project-level context, such as repository root and project-specific instructions (AGENTS.md/GEMINI.md).
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
	std::string get_repository_root() const
	{
		return repo_root_;
	}

	/**
	 * @brief Returns the content of AGENTS.md or GEMINI.md if found at the root.
	 */
	std::string get_project_instructions() const
	{
		return instructions_;
	}

	/**
	 * @brief Returns a minified version of .clang-format if it exists and is under 100 lines.
	 */
	std::string get_clang_format() const
	{
		return clang_format_;
	}

	/**
	 * @brief Returns a markdown representation of the project layout (top directories).
	 */
	std::string get_project_layout_markdown() const;

	/**
	 * @brief Returns a unified markdown block containing all project-level knowledge (instructions, format, layout).
	 */
	std::string get_project_knowledge_prompt() const;

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
	std::vector<lsp_manager::symbol_info> lsp_query_workspace_symbols(const std::string &query);
	std::vector<lsp_manager::call_hierarchy_item> lsp_query_call_hierarchy_outgoing(const std::string &filepath, int line, int character);

	// Test management
	std::vector<std::string> get_available_tests();
	void refresh_available_tests();

	// Software Map (Background LSP)
	std::string get_software_map_markdown() const;

      private:
	project_manager() = default;

	void load_instructions();
	void inventory_project(std::stop_token stop);
	void software_map_loop(std::stop_token stop);

	struct directory_info {
		std::string path;
		int direct_files{0};
		int direct_headers{0};
		int direct_docs_config{0};
		int total_files_underneath{0};
		int depth{0};
	};

	struct project_layout {
		std::vector<directory_info> top_directories;
		std::vector<std::string> key_files;
		bool ready{false};
	};

	struct software_map_symbol {
		std::string name;
		int kind; // LSP SymbolKind (5=Class, 12=Function, etc)
		lsp_manager::location_info location;
		int looked_up_count{0};
		int accumulated_count{0};
		bool is_seed{true}; // True if found via initial workspace/symbol scan
	};

	struct software_map_data {
		std::string git_head_hash;
		std::vector<software_map_symbol> symbols;
		bool ready{false};
	};

	std::string repo_root_;
	std::string instructions_;
	std::string clang_format_;
	std::unique_ptr<lsp_manager> lsp_manager_;

	std::vector<std::string> available_tests_;
	bool tests_ready_{false};

	mutable std::mutex layout_mutex_;
	project_layout layout_;
	std::jthread inventory_thread_;

	mutable std::mutex software_map_mutex_;
	software_map_data software_map_;
	std::jthread software_map_thread_;
};
