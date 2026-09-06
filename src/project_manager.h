#pragma once
#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
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
	void shutdown();

	/**
	 * @brief Returns the absolute path to the project root.
	 */
	std::string get_project_root() const
	{
		assert((!enforce_initialization_ || initialized_) && "project_manager must be initialized before calling get_project_root");
		return project_root_;
	}

	void set_project_root(std::string_view root)
	{
		project_root_ = root;
		invalidate_available_tests_cache();
	}

	void set_enforce_initialization(bool enforce)
	{
		enforce_initialization_ = enforce;
	}

	bool is_exiting() const
	{
		return is_exiting_;
	}

	void set_exiting(bool exiting)
	{
		is_exiting_ = exiting;
		if (exiting) {
			inventory_thread_.request_stop();
		}
	}

	bool is_editor_mode() const
	{
		return is_editor_mode_;
	}

	void set_editor_mode(bool editor_mode)
	{
		is_editor_mode_ = editor_mode;
	}

	/**
	 * @brief Returns the content of AGENTS.md or GEMINI.md if found at the root.
	 */
	std::string get_project_instructions() const
	{
		assert((!enforce_initialization_ || initialized_) && "project_manager must be initialized before calling get_project_instructions");
		return instructions_;
	}

	/**
	 * @brief Returns a minified version of .clang-format if it exists and is under 100 lines.
	 */
	std::string get_clang_format() const
	{
		assert((!enforce_initialization_ || initialized_) && "project_manager must be initialized before calling get_clang_format");
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

	/**
	 * @brief Returns the list of detected dependency names from meson.build.
	 */
	std::vector<std::string> get_detected_dependencies() const;

	/**
	 * @brief Returns the list of recognized dependency github:// VFS URLs.
	 */
	std::vector<std::string> get_github_vfs_urls() const;

	/**
	 * @brief Returns the mapping of recognized dependencies to their VFS paths.
	 */
	std::vector<std::pair<std::string, std::string>> get_mapped_dependencies() const;

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
	std::vector<lsp_manager::symbol_node> lsp_query_document_symbols(const std::string &filepath);
	void lsp_invalidate_symbol_cache(const std::string &filepath);
	std::vector<lsp_manager::call_hierarchy_item> lsp_query_call_hierarchy_outgoing(const std::string &filepath, int line,
											int character);
	std::vector<lsp_manager::outgoing_call_item> lsp_query_call_hierarchy_outgoing_batch(
		const std::string &filepath,
		const std::vector<std::pair<int, int>> &positions,
		std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max());
	std::vector<lsp_manager::type_hierarchy_item> lsp_query_type_hierarchy_supertypes(const std::string &filepath, int line,
											  int character);
	std::optional<std::vector<diagnostic_info>> lsp_query_file_diagnostics(const std::string &filepath);

	// Test management
	std::vector<std::string> get_available_tests();
	void refresh_available_tests();
	// Forces the cached available-test list to be rebuilt on the next call to
	// get_available_tests(). Used when the build definition may have changed
	// (e.g. meson.build edited) or when a lookup for a requested test name missed
	// (so newly-registered tests become discoverable without a restart).
	void invalidate_available_tests_cache() noexcept;
	// Parses the raw output of `ctest -N` (or `ctest --show-only`) and extracts test names.
	static std::vector<std::string> parse_ctest_test_list(std::string_view output);
	// Resolves the build directory used for test listing and project builds. Prefers the
	// configured build directory; if empty or lacking build artifacts (build.ninja, CMakeCache.txt,
	// CTestTestfile.cmake, Makefile), falls back to a build directory found under the project root
	// (e.g. build/). Returns empty if none is usable.
	std::string resolve_build_dir() const;

	// Executable candidate scanning
	std::vector<std::string> detect_executable_candidates();



      private:
	project_manager();
	~project_manager();

	void load_instructions();
	void scan_dependencies();
	void inventory_project(std::stop_token stop);
	// Returns true if the build definition file (meson.build or CMakeLists.txt) used to populate
	// the available-test list has a different mtime than when the list was last refreshed.
	// Used by get_available_tests() to invalidate the cached list when build definitions
	// change (e.g. new test targets added).
	bool build_definition_changed() const;

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

	std::string project_root_;
	std::string instructions_;
	std::string clang_format_;
	std::unique_ptr<lsp_manager> lsp_manager_;

	/*
	 * available_tests_mutex_ protects available_tests_, tests_ready_,
	 * tests_build_def_mtime_, and tests_list_refreshed_at_.
	 * Locking Rules:
	 * - Synchronizes access across queries, invalidations, and background/tool-driven refreshes.
	 */
	mutable std::mutex available_tests_mutex_;
	std::vector<std::string> available_tests_;
	bool tests_ready_{false};
	// Last-modified time of the build definition file (meson.build or CMakeLists.txt) seen when the
	// available-test list was last refreshed. When get_available_tests() is called
	// and this timestamp is older than the on-disk build definition mtime, the cached
	// list is considered stale because newly-registered test binaries would not
	// appear in test list output.
	std::filesystem::file_time_type tests_build_def_mtime_;
	// Wall-clock time (steady clock) when the available-test list was last
	// refreshed. Provides an upper bound on staleness: even if the build definition is
	// unchanged, a very old cached list is refreshed so a long-running editor
	// session does not miss tests added/changed through other means (e.g. a
	// different working copy checked out, generated test fixtures, etc.),
	// while still caching effectively for back-to-back lookups.
	std::chrono::steady_clock::time_point tests_list_refreshed_at_;

	/*
	 * layout_mutex_ protects the project_layout structure.
	 * Locking Rules:
	 * - Held briefly when querying or updating the cached project layout during directory inventory.
	 */
	mutable std::mutex layout_mutex_;
	project_layout layout_;
	std::jthread inventory_thread_;



	/*
	 * dependencies_mutex_ protects detected_dependencies_, github_vfs_urls_, and mapped_dependencies_.
	 * Locking Rules:
	 * - Held briefly when scanning meson.build during initialization or when querying dependencies or GitHub VFS URLs.
	 * - Read and write access is synchronized using std::lock_guard.
	 */
	mutable std::mutex dependencies_mutex_;
	std::vector<std::string> detected_dependencies_;
	std::vector<std::string> github_vfs_urls_;
	std::vector<std::pair<std::string, std::string>> mapped_dependencies_;

	std::atomic<bool> is_exiting_{false};
	std::atomic<bool> initialized_{false};
	std::atomic<bool> enforce_initialization_{false};
	std::atomic<bool> is_editor_mode_{false};
};
