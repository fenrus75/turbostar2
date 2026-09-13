#pragma once

#include <cstddef>
#include <istream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

// Holds index information for a function definition and its outgoing calls
struct semcode_function_entry {
	std::string name;
	std::string file_path;
	int line_start{0};
	int line_end{0};
	std::vector<std::string> calls;
	std::vector<std::string> types;
};

// Holds index information for a type or typedef definition
struct semcode_type_entry {
	std::string name;
	std::string file_path;
	int line_start{0};
	int line_end{0};
	std::string kind;
	std::string underlying_type;
};

class semcode_indexer
{
      public:
	explicit semcode_indexer(std::string_view db_path);
	~semcode_indexer();

	semcode_indexer(const semcode_indexer &) = delete;
	semcode_indexer &operator=(const semcode_indexer &) = delete;
	semcode_indexer(semcode_indexer &&) noexcept;
	semcode_indexer &operator=(semcode_indexer &&) noexcept;

	[[nodiscard]] bool open();
	void close();
	[[nodiscard]] bool is_open() const noexcept;

	// Ingests function definitions from a stream or JSON file
	size_t ingest_functions_stream(std::istream &input);
	size_t ingest_functions_file(const std::string &untrusted_json_path);

	// Ingests type definitions from a stream or JSON file
	size_t ingest_types_stream(std::istream &input);
	size_t ingest_types_file(const std::string &untrusted_json_path);

	/**
	 * @brief Runs semcode CLI on a project to dump functions and types to temporary JSON files,
	 * ingests them, builds B-tree indices, records metadata, and prunes stale databases in the destination directory.
	 * @param untrusted_project_dir Path to the git project directory.
	 * @param semcode_bin Path or binary name for the semcode CLI.
	 * @param max_dbs Maximum number of database files to retain in destination directory (default: 2).
	 * @return True on success, false if semcode invocation or ingestion failed.
	 */
	[[nodiscard]] bool build_from_project(const std::string &untrusted_project_dir, std::string_view semcode_bin = "semcode",
					      size_t max_dbs = 2);

	// Builds indexes on tables after bulk loading
	[[nodiscard]] bool build_indices();

	// Metadata management (e.g. git_head, function_count, created_at)
	[[nodiscard]] bool set_metadata(std::string_view key, std::string_view value);
	[[nodiscard]] std::string get_metadata(std::string_view key) const;

	// Query interface
	[[nodiscard]] std::vector<semcode_function_entry> lookup_function(std::string_view name) const;
	[[nodiscard]] std::vector<semcode_function_entry> lookup_functions_in_file(std::string_view file_path, int start_line,
										   int end_line) const;
	[[nodiscard]] std::vector<semcode_type_entry> lookup_type(std::string_view name) const;

	/**
	 * @brief Prunes old semcode database files (*.db and companion files) in a directory, keeping at most max_dbs copies.
	 * Sorts databases by last modification time (most recent first) and removes the oldest ones.
	 * @param untrusted_dir Directory path containing the databases.
	 * @param max_dbs Maximum number of database files to retain (default: 2).
	 * @return Number of database files removed.
	 */
	static size_t prune_cache_directory(const std::string &untrusted_dir, size_t max_dbs = 2);

	/**
	 * @brief Updates the last write/modification time of the database to the current time for LRU tracking.
	 */
	static void touch_database(const std::string &untrusted_db_path);

      private:
	std::string db_path_;
	sqlite3 *db_{nullptr};

	[[nodiscard]] bool init_schema();
};
