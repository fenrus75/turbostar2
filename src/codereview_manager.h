#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <shared_mutex>
#include <string>
#include <vector>

struct review_item {
	int id{0};
	uint64_t datestamp{0}; // unix time
	std::string git_hash;
	std::string summary;
	std::string filename;
	int line_number{0}; // 1-based, 0 if optional/none
	std::string line_content;
	std::string state;    // invalid, new, confirmed, disputed, stale, resolved, verified-fixed
	std::string severity; // nit, low, medium, high, critical
	std::string description;
	std::string proposed_fix;
	std::string resolved_in_commit;
};

// Conversions to/from JSON for review_item
void to_json(nlohmann::json &j, const review_item &item);
void from_json(const nlohmann::json &j, review_item &item);

class codereview_manager
{
      public:
	static codereview_manager &get_instance();

	// Load code reviews from review.json in the project cache directory
	void load_project(const std::string &project_root_path);
	// Force immediate save of code reviews to review.json
	void save_project() const;

	// Create a new code review item. Returns the new item's unique ID.
	int create_code_review_item(const std::string &summary, const std::string &filename, int line_number,
				    const std::string &line_content, const std::string &severity, const std::string &description,
				    const std::string &proposed_fix);

	bool update_code_review_item(int id, const std::optional<std::string> &state, const std::optional<std::string> &severity,
				     const std::optional<std::string> &description, const std::optional<std::string> &proposed_fix,
				     const std::optional<std::string> &summary = std::nullopt);

	// Transition state: new -> confirmed, or resolved -> verified-fixed
	bool confirm_code_review_item(int id);

	// Transition state to resolved with a resolved_in commit hash
	bool resolve_code_review_item(int id, const std::string &commit_hash);

	// Retrieve a copy of a single code review item by ID.
	std::optional<review_item> get_code_review_item(int id) const;

	// List all code review items, with optional filters.
	std::vector<review_item> list_code_review_items(const std::string &filename_filter = "", const std::string &severity_filter = "",
							bool include_resolved = false) const;

	// Clear all code review items and reset counters
	void clear_all();

      private:
	codereview_manager() = default;
	~codereview_manager() = default;

	// Helper to get filepath for review.json
	std::string get_review_json_path() const;

	// Helper to get current git hash using sync_command_runner
	std::string get_current_git_hash() const;

	// Helper to perform serialization to review.json while mutex is held
	void save_project_unlocked() const;

	// Internal in-memory list of items
	std::vector<review_item> items_;
	// The next available unique ID (only ever increments)
	int next_id_{1};
	// Root path of the current project
	std::string project_root_path_;

	/*
	 * mutex_ is a shared reader-writer mutex protecting the items_ list, next_id_ counter,
	 * and project_root_path_ state.
	 * Locking Rules:
	 * - Shared locks (readers) are used when listing items, getting details, or checking sizes.
	 * - Exclusive locks (writers) are used when loading, saving, creating, updating, confirming,
	 *   resolving, or clearing code review items.
	 */
	mutable std::shared_mutex mutex_;
};
