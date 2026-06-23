#pragma once
#include <map>
#include <mutex>
#include <string>

/**
 * @brief Manages global usage statistics, persisting them to ~/.cache/turbostar/statistics.json.
 */
class statistics_manager
{
      public:
	static statistics_manager &get_instance();

	// Disallow copy/move to enforce singleton
	statistics_manager(const statistics_manager &) = delete;
	statistics_manager &operator=(const statistics_manager &) = delete;

	/**
	 * @brief Increments the value of a specific statistical key.
	 *        If the key does not exist, it is initialized to the specified amount.
	 * @param key The telemetry key to increment.
	 * @param amount The value to add (defaults to 1).
	 */
	void increment_stat(const std::string &key, int amount = 1);

	/**
	 * @brief Gets the current value of a specific key.
	 * @return The value of the stat, or 0 if it doesn't exist.
	 */
	int get_stat(const std::string &key) const;

	/**
	 * @brief Retrieves a copy of the entire statistics map.
	 */
	std::map<std::string, int> get_all_stats() const;

	/**
	 * @brief Loads statistics from ~/.cache/turbostar/statistics.json.
	 */
	void load();

	/**
	 * @brief Saves the current statistics database to disk.
	 */
	void save() const;

      private:
	statistics_manager() = default;
	std::string get_stats_file_path() const;
	void save_unlocked() const;

	/*
	 * mutex_ protects access to the stats_ member variable.
	 *
	 * Locking Rules:
	 * - Lock mutex_ exclusively (via std::lock_guard) before reading or writing to stats_.
	 * - Must be acquired and released within the scope of public API calls to ensure
	 *   thread-safety when accessed by background agent execution threads.
	 * - No nested locking of other class-level mutexes is performed while holding mutex_.
	 */
	mutable std::mutex mutex_;
	std::map<std::string, int> stats_;
};
