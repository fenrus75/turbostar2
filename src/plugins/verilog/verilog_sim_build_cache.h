#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <future>
#include <memory>
#include <mutex>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace verilog
{

struct sim_compile_request {
	std::filesystem::path root_path;
	std::string project_root;
	std::string display_tb;
	std::string top_mod;
	std::vector<std::string> all_sources;
	std::filesystem::path mdir;
	std::filesystem::path sim_bin;
	size_t build_jobs{1};
	bool enable_coverage{false};
	bool has_formal{false};
};

struct sim_compile_result {
	std::string top_mod;
	std::vector<std::string> all_sources;
	std::filesystem::path mdir;
	std::filesystem::path sim_bin;
	bool compile_failed{false};
	bool cache_hit{false};
	bool joined_in_flight{false};
	std::string error_markdown;
};

/**
 * @brief Build fingerprint cache, in-flight compile deduplicator, and debounced AOT
 *        pre-compiler for Verilator simulation testbenches.
 *
 * Layer 1 (always active): Computes a fast signature over (flags, transitive source
 * paths, sizes, and mtimes) stored in `<mdir>/.turbostar_build.sig`. If `V<top>` is
 * already up to date, compilation is skipped in 0ms. Concurrent compiles for the same
 * `--Mdir` are deduplicated/serialized via an in-flight future table.
 *
 * Layer 2 (session-gated AOT pre-compiler): Only activates after at least one
 * `verilog_simulate` call has run in the current non-CLI session. Debounces `.sv`/`.v`
 * file edits and pre-compiles affected `quick` testbenches in the background using
 * at most half of available CPU cores.
 */
class verilog_sim_build_cache
{
      public:
	static verilog_sim_build_cache &get_instance();

	verilog_sim_build_cache(const verilog_sim_build_cache &) = delete;
	verilog_sim_build_cache &operator=(const verilog_sim_build_cache &) = delete;

	// Layer 1 helpers
	static std::vector<std::string> collect_tb_sources(const std::string &tb_file,
	                                                   std::span<const std::string> rtl_files,
	                                                   std::span<const std::string> formal_files);

	static std::filesystem::path compute_mdir_path(const std::filesystem::path &root_path,
	                                               std::string_view display_tb,
	                                               std::string_view top_mod,
	                                               size_t plan_idx,
	                                               bool enable_coverage,
	                                               bool has_formal);

	static std::string compute_build_signature(std::span<const std::string> all_sources,
	                                           std::string_view top_mod,
	                                           bool enable_coverage,
	                                           bool has_formal);

	static bool is_build_up_to_date(const std::filesystem::path &mdir,
	                                const std::filesystem::path &sim_bin,
	                                std::string_view expected_sig);

	static void write_build_signature(const std::filesystem::path &mdir, std::string_view sig);

	// Removes intermediate Verilator artifacts (*.gch, *.o, *.a, *.cpp, *.h, *.d, *.mk) from mdir,
	// preserving only sim_bin, .turbostar_build.sig, and coverage.dat (~225 MB -> ~400 KB per TB).
	static void prune_intermediate_mdir_artifacts(const std::filesystem::path &mdir,
	                                              const std::filesystem::path &sim_bin);

	// Process-wide RAII concurrency permit bounding simultaneous Verilator/Yosys worker processes.
	class scoped_eda_permit
	{
	      public:
		explicit scoped_eda_permit(verilog_sim_build_cache &cache);
		~scoped_eda_permit();
		scoped_eda_permit(const scoped_eda_permit &) = delete;
		scoped_eda_permit &operator=(const scoped_eda_permit &) = delete;

	      private:
		verilog_sim_build_cache &cache_;
	};

	void acquire_eda_permit();
	void release_eda_permit() noexcept;

	// Compiles via Verilator, returns immediately on cache hit, or joins an identical in-flight build.
	sim_compile_result compile_or_join(const sim_compile_request &req);

	// Layer 2 AOT pre-compiler controls
	void record_simulation_session(std::string_view project_root, bool enable_coverage, bool include_formal);
	void begin_simulate_call() noexcept;
	void end_simulate_call() noexcept;
	void on_file_changed(std::string_view filepath);
	void shutdown();

	// RAII guard marking an active verilog_simulate() call so AOT yields immediately
	struct scoped_simulate_guard {
		explicit scoped_simulate_guard(verilog_sim_build_cache &c) noexcept : cache_(c) { cache_.begin_simulate_call(); }
		~scoped_simulate_guard() { cache_.end_simulate_call(); }
		scoped_simulate_guard(const scoped_simulate_guard &) = delete;
		scoped_simulate_guard &operator=(const scoped_simulate_guard &) = delete;
	      private:
		verilog_sim_build_cache &cache_;
	};

	// Exposed for unit testing (returns whether AOT session gate is active)
	bool is_aot_session_active() const noexcept;
	void reset_session_for_testing();

      private:
	verilog_sim_build_cache() = default;
	~verilog_sim_build_cache();

	struct in_flight_build {
		std::string signature;
		std::shared_future<sim_compile_result> future;
	};

	void worker_loop(std::stop_token stoken);
	void invalidate_affected_binaries(const std::string &project_root, const std::string &changed_file);
	void run_aot_precompile_batch(const std::string &project_root,
	                              const std::set<std::string> &changed_files,
	                              bool enable_coverage,
	                              bool include_formal,
	                              uint64_t expected_generation,
	                              std::stop_token stoken);

	/*
	 * in_flight_mutex_ protects in_flight_builds_ (active Verilator builds keyed by canonical mdir).
	 * Locking Rules:
	 * - Held briefly to look up, insert, or erase in-flight build entries.
	 * - Never held while waiting on a future or executing Verilator.
	 */
	std::mutex in_flight_mutex_;
	std::unordered_map<std::string, std::shared_ptr<in_flight_build>> in_flight_builds_;

	/*
	 * aot_mutex_ protects session_active_, aot_project_root_, aot_enable_coverage_,
	 * aot_include_formal_, pending_files_, has_pending_work_, debounce_deadline_,
	 * edit_generation_, and worker_started_.
	 * Locking Rules:
	 * - Acquired with worker_cv_ for debounced wakeup and shutdown coordination.
	 * - Never held while running Verilator or scanning files on disk.
	 */
	mutable std::mutex aot_mutex_;
	std::condition_variable_any worker_cv_;
	bool session_active_{false};
	bool worker_started_{false};
	std::string aot_project_root_;
	bool aot_enable_coverage_{false};
	bool aot_include_formal_{true};
	std::set<std::string> pending_files_;
	bool has_pending_work_{false};
	std::chrono::steady_clock::time_point debounce_deadline_{};
	uint64_t edit_generation_{0};
	std::atomic<int> active_simulate_calls_{0};
	std::jthread worker_thread_;

	/*
	 * eda_permit_mutex_ protects active_eda_jobs_ and synchronizes eda_permit_cv_.
	 * Locking Rules:
	 * - Held briefly when acquiring or releasing a process-wide EDA concurrency permit.
	 * - Never held while executing Verilator or Yosys subprocesses.
	 */
	std::mutex eda_permit_mutex_;
	std::condition_variable eda_permit_cv_;
	size_t active_eda_jobs_{0};
};

} // namespace verilog
