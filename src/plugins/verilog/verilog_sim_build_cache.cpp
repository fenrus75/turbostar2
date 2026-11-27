#include "plugins/verilog/verilog_sim_build_cache.h"

#include <algorithm>
#include <cctype>
#include <ctre.hpp>
#include <format>
#include <fstream>
#include <set>
#include <sstream>

#include "fs_utils.h"
#include "mapped_file.h"
#include "plugins/verilog/fpga_project_settings.h"
#include "plugins/verilog/verilog_module_scanner.h"

namespace fs = std::filesystem;

namespace verilog
{

namespace
{

constexpr const char *kSigFilename = ".turbostar_build.sig";
constexpr auto kDebounceDelay = std::chrono::milliseconds(1500);

bool is_verilog_source_extension(std::string_view filepath) noexcept
{
	std::string ext = fs::path(filepath).extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return ext == ".sv" || ext == ".v" || ext == ".svh" || ext == ".vh";
}

// Extract target module names from `bind <target_mod> ...` inside a formal property file.
std::set<std::string> extract_bind_targets(const std::string &formal_file)
{
	std::set<std::string> targets;
	std::string err;
	auto mf = mapped_file::get_cached(formal_file, err, /*populate=*/false);
	if (!mf) {
		return targets;
	}
	std::string clean = verilog_module_scanner::strip_comments(mf->string_view());
	const char *start = clean.data();
	const char *end = clean.data() + clean.size();
	while (start < end) {
		auto m = ctre::search<R"(\bbind\s+([a-zA-Z_][a-zA-Z_0-9]*)\b)">(start, end);
		if (!m) {
			break;
		}
		targets.insert(m.get<1>().to_string());
		start = m.data() + m.size();
	}
	return targets;
}

bool is_package_or_header_file(const std::string &filepath)
{
	std::string fname = fs::path(filepath).filename().string();
	std::string lower = fname;
	std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	if (lower.ends_with(".svh") || lower.ends_with(".vh") ||
	    lower.find("_pkg.") != std::string::npos || lower.find("pkg.") != std::string::npos) {
		return true;
	}
	std::string err;
	auto mf = mapped_file::get_cached(filepath, err, /*populate=*/false);
	if (!mf) {
		return false;
	}
	std::string_view sv = mf->string_view();
	return sv.find("package ") != std::string_view::npos || sv.find("interface ") != std::string_view::npos;
}

uint64_t fnv1a_hash_update(uint64_t h, std::string_view data) noexcept
{
	for (unsigned char c : data) {
		h ^= static_cast<uint64_t>(c);
		h *= 1099511628211ULL;
	}
	return h;
}

} // namespace

verilog_sim_build_cache &verilog_sim_build_cache::get_instance()
{
	static verilog_sim_build_cache instance;
	return instance;
}

verilog_sim_build_cache::~verilog_sim_build_cache()
{
	shutdown();
}

std::vector<std::string> verilog_sim_build_cache::collect_tb_sources(const std::string &tb_file,
                                                                     std::span<const std::string> rtl_files,
                                                                     std::span<const std::string> formal_files)
{
	std::error_code ec;
	fs::path canon_tb = fs::weakly_canonical(tb_file, ec);
	ec.clear();

	// Include ALL instantiated modules (not just preferred DUT) so helper/BRAM modules
	// instantiated alongside the DUT (e.g. `cpu dut` + `initialized_bram mem`) are always compiled.
	auto direct_mods = verilog_module_scanner::discover_instantiated_modules(
		tb_file, rtl_files, /*include_all_instantiations=*/true);
	std::set<std::string> needed_mods;
	for (const auto &m : direct_mods) {
		needed_mods.insert(m);
		auto hier = verilog_module_scanner::discover_submodule_hierarchy(m, rtl_files);
		for (const auto &hm : hier) {
			needed_mods.insert(hm);
		}
	}

	std::vector<std::string> all_sources;
	std::set<std::string> seen_canon;

	if (!needed_mods.empty()) {
		auto discovered = verilog_module_scanner::discover_modules_in_files(rtl_files);
		std::set<std::string> needed_files;
		for (const auto &dm : discovered) {
			if (needed_mods.contains(dm.module_name)) {
				fs::path cf = fs::weakly_canonical(dm.source_file, ec);
				needed_files.insert(ec ? dm.source_file : cf.string());
				ec.clear();
			}
		}
		for (const auto &f : rtl_files) {
			fs::path cf = fs::weakly_canonical(f, ec);
			std::string cf_str = ec ? f : cf.string();
			ec.clear();
			if (cf == canon_tb) {
				continue;
			}
			if ((needed_files.contains(cf_str) || is_package_or_header_file(f)) && seen_canon.insert(cf_str).second) {
				all_sources.push_back(f);
			}
		}
		for (const auto &ff : formal_files) {
			fs::path cf = fs::weakly_canonical(ff, ec);
			std::string cf_str = ec ? ff : cf.string();
			ec.clear();
			if (cf == canon_tb) {
				continue;
			}
			auto bind_targets = extract_bind_targets(ff);
			bool relevant = bind_targets.empty();
			for (const auto &bt : bind_targets) {
				if (needed_mods.contains(bt)) {
					relevant = true;
					break;
				}
			}
			if (relevant && seen_canon.insert(cf_str).second) {
				all_sources.push_back(ff);
			}
		}
	} else {
		// Fallback when testbench instantiations cannot be statically resolved
		for (const auto &f : rtl_files) {
			fs::path cf = fs::weakly_canonical(f, ec);
			std::string cf_str = ec ? f : cf.string();
			ec.clear();
			if (cf != canon_tb && seen_canon.insert(cf_str).second) {
				all_sources.push_back(f);
			}
		}
		for (const auto &ff : formal_files) {
			fs::path cf = fs::weakly_canonical(ff, ec);
			std::string cf_str = ec ? ff : cf.string();
			ec.clear();
			if (cf != canon_tb && seen_canon.insert(cf_str).second) {
				all_sources.push_back(ff);
			}
		}
	}

	all_sources.push_back(tb_file);
	return all_sources;
}

fs::path verilog_sim_build_cache::compute_mdir_path(const fs::path &root_path,
                                                    std::string_view display_tb,
                                                    std::string_view top_mod,
                                                    size_t plan_idx,
                                                    bool enable_coverage,
                                                    bool has_formal)
{
	std::error_code ec;
	std::string tb_stem = fs::path(display_tb).stem().string();
	std::string safe_stem;
	safe_stem.reserve(tb_stem.size());
	for (unsigned char c : tb_stem) {
		safe_stem.push_back(std::isalnum(c) ? static_cast<char>(c) : '_');
	}
	std::string mdir_leaf;
	if (safe_stem.empty() || safe_stem == top_mod) {
		mdir_leaf = std::format("verilator_{}", top_mod);
	} else {
		mdir_leaf = std::format("verilator_{}_{}", safe_stem, top_mod);
	}
	if (plan_idx > 0 && safe_stem.empty()) {
		mdir_leaf += std::format("_{}", plan_idx);
	}
	if (enable_coverage) {
		mdir_leaf += "_cov";
	}
	if (has_formal) {
		mdir_leaf += "_formal";
	}

	fs::path base_tmp = (fs::weakly_canonical(root_path, ec) == fs::weakly_canonical(fs::current_path(ec), ec))
		? fs::path(fs_utils::get_project_tmp_dir())
		: (root_path / ".turbostar_tmp");
	ec.clear();
	return fs::absolute(base_tmp / mdir_leaf, ec);
}

std::string verilog_sim_build_cache::compute_build_signature(std::span<const std::string> all_sources,
                                                             std::string_view top_mod,
                                                             bool enable_coverage,
                                                             bool has_formal)
{
	std::error_code ec;
	std::vector<std::string> entries;
	entries.reserve(all_sources.size());

	std::set<std::string> seen_canon;
	auto add_file_entry = [&](const fs::path &raw_path) {
		fs::path p = fs::weakly_canonical(raw_path, ec);
		std::string canon = ec ? raw_path.string() : p.string();
		ec.clear();
		if (!seen_canon.insert(canon).second) {
			return;
		}
		uintmax_t sz = fs::file_size(p, ec);
		if (ec) {
			sz = 0;
			ec.clear();
		}
		auto ft = fs::last_write_time(p, ec);
		long long mtime_ns = ec ? 0LL : std::chrono::duration_cast<std::chrono::nanoseconds>(ft.time_since_epoch()).count();
		ec.clear();
		entries.push_back(std::format("{}|{}|{}", canon, sz, mtime_ns));
	};

	for (const auto &src : all_sources) {
		fs::path sp(src);
		add_file_entry(sp);

		// Also hash any `include "..." headers referenced by this source file
		std::string err;
		if (auto mf = mapped_file::get_cached(src, err, /*populate=*/false)) {
			std::string_view sv = mf->string_view();
			const char *start = sv.data();
			const char *end = sv.data() + sv.size();
			while (start < end) {
				auto m = ctre::search<R"re(`include\s+"([^"]+)")re">(start, end);
				if (!m) {
					break;
				}
				std::string inc_rel = m.get<1>().to_string();
				fs::path cand = sp.parent_path() / inc_rel;
				if (fs::is_regular_file(cand, ec)) {
					add_file_entry(cand);
				}
				ec.clear();
				start = m.data() + m.size();
			}
		}
	}
	std::sort(entries.begin(), entries.end());

	uint64_t h = 14695981039346656037ULL;
	h = fnv1a_hash_update(h, "v2|");
	h = fnv1a_hash_update(h, top_mod);
	h = fnv1a_hash_update(h, enable_coverage ? "|cov1" : "|cov0");
	h = fnv1a_hash_update(h, has_formal ? "|frm1" : "|frm0");
	for (const auto &e : entries) {
		h = fnv1a_hash_update(h, "|");
		h = fnv1a_hash_update(h, e);
	}
	return std::format("{:016x}", h);
}

bool verilog_sim_build_cache::is_build_up_to_date(const fs::path &mdir,
                                                  const fs::path &sim_bin,
                                                  std::string_view expected_sig)
{
	if (expected_sig.empty()) {
		return false;
	}
	std::error_code ec;
	if (!fs::is_regular_file(sim_bin, ec)) {
		return false;
	}
	fs::path sig_path = mdir / kSigFilename;
	if (!fs::is_regular_file(sig_path, ec)) {
		return false;
	}
	std::ifstream in(sig_path, std::ios::binary);
	if (!in.is_open()) {
		return false;
	}
	std::string stored;
	std::getline(in, stored);
	while (!stored.empty() && (stored.back() == '\r' || stored.back() == '\n' || stored.back() == ' ')) {
		stored.pop_back();
	}
	return stored == expected_sig;
}

void verilog_sim_build_cache::write_build_signature(const fs::path &mdir, std::string_view sig)
{
	std::error_code ec;
	fs::create_directories(mdir, ec);
	std::ofstream out(mdir / kSigFilename, std::ios::binary | std::ios::trunc);
	if (out.is_open()) {
		out << sig << "\n";
	}
}

void verilog_sim_build_cache::prune_intermediate_mdir_artifacts(const fs::path &mdir, const fs::path &sim_bin)
{
	std::error_code ec;
	if (!fs::is_directory(mdir, ec)) {
		return;
	}
	std::string keep_bin = sim_bin.filename().string();
	for (const auto &entry : fs::directory_iterator(mdir, ec)) {
		if (ec) {
			break;
		}
		std::string name = entry.path().filename().string();
		if (name == keep_bin || name == kSigFilename || name == "coverage.dat") {
			continue;
		}
		if (entry.is_regular_file(ec) || entry.is_symlink(ec)) {
			fs::remove(entry.path(), ec);
			ec.clear();
		}
	}
}

verilog_sim_build_cache::scoped_eda_permit::scoped_eda_permit(verilog_sim_build_cache &cache) : cache_(cache)
{
	cache_.acquire_eda_permit();
}

verilog_sim_build_cache::scoped_eda_permit::~scoped_eda_permit()
{
	cache_.release_eda_permit();
}

void verilog_sim_build_cache::acquire_eda_permit()
{
	unsigned int hw = std::thread::hardware_concurrency();
	const size_t max_jobs = std::max<size_t>(2, hw == 0 ? 4u : hw);
	std::unique_lock<std::mutex> lk(eda_permit_mutex_);
	eda_permit_cv_.wait(lk, [&]() { return active_eda_jobs_ < max_jobs; });
	++active_eda_jobs_;
}

void verilog_sim_build_cache::release_eda_permit() noexcept
{
	{
		std::lock_guard<std::mutex> lk(eda_permit_mutex_);
		if (active_eda_jobs_ > 0) {
			--active_eda_jobs_;
		}
	}
	eda_permit_cv_.notify_one();
}

sim_compile_result verilog_sim_build_cache::compile_or_join(const sim_compile_request &req)
{
	std::error_code ec;
	std::string expected_sig = compute_build_signature(req.all_sources, req.top_mod, req.enable_coverage, req.has_formal);
	std::string mdir_key = fs::weakly_canonical(req.mdir, ec).string();
	if (ec || mdir_key.empty()) {
		mdir_key = req.mdir.string();
		ec.clear();
	}

	// Coordinate with any in-flight build targeting the exact same --Mdir
	std::shared_ptr<std::promise<sim_compile_result>> my_promise;
	while (true) {
		std::shared_future<sim_compile_result> wait_future;
		bool same_sig = false;

		{
			std::lock_guard<std::mutex> lk(in_flight_mutex_);
			auto it = in_flight_builds_.find(mdir_key);
			if (it != in_flight_builds_.end()) {
				wait_future = it->second->future;
				same_sig = (it->second->signature == expected_sig);
			} else {
				// Fast-path cache hit before registering a new build
				if (is_build_up_to_date(req.mdir, req.sim_bin, expected_sig)) {
					sim_compile_result hit;
					hit.top_mod = req.top_mod;
					hit.all_sources = req.all_sources;
					hit.mdir = req.mdir;
					hit.sim_bin = req.sim_bin;
					hit.cache_hit = true;
					return hit;
				}
				my_promise = std::make_shared<std::promise<sim_compile_result>>();
				auto entry = std::make_shared<in_flight_build>();
				entry->signature = expected_sig;
				entry->future = my_promise->get_future().share();
				in_flight_builds_[mdir_key] = std::move(entry);
				break;
			}
		}

		// Wait outside the lock
		sim_compile_result joined = wait_future.get();
		if (same_sig) {
			joined.joined_in_flight = true;
			return joined;
		}
		// Stale build finished; loop around to check cache or start our fresh build
	}

	// Perform the actual Verilator compilation
	sim_compile_result res;
	res.top_mod = req.top_mod;
	res.all_sources = req.all_sources;
	res.mdir = req.mdir;
	res.sim_bin = req.sim_bin;

	fs::create_directories(req.mdir, ec);
	ec.clear();
	fs::remove(req.mdir / kSigFilename, ec);
	ec.clear();
	if (fs::exists(req.sim_bin, ec)) {
		fs::remove(req.sim_bin, ec);
	}
	ec.clear();

	// Share compiled Verilator runtime & unchanged .o/.gch objects across all testbenches
	// via a writable, size-capped project-local CCACHE_DIR (safe under read-only-HOME sandbox).
	std::string ccache_env = "CCACHE_DISABLE=1 OBJCACHE=";
	if (!fs_utils::find_executable("ccache").empty()) {
		fs::path ccache_dir = req.mdir.parent_path() / "ccache";
		fs::create_directories(ccache_dir, ec);
		ec.clear();
		ccache_env = std::format("CCACHE_DIR={} CCACHE_MAXSIZE=2G CCACHE_SLOPPINESS=pch_defines,time_macros OBJCACHE=ccache",
		                         fs_utils::escape_shell_arg(ccache_dir.string()));
	}

	std::string compile_cmd = std::format(
		"cd {} && {} verilator --binary --assert --build-jobs {} --timing --trace --timescale 1ns/1ps -Wno-TIMESCALEMOD{} --Mdir {} --top-module {}",
		fs_utils::escape_shell_arg(req.project_root),
		ccache_env,
		std::max<size_t>(1, req.build_jobs),
		req.enable_coverage ? " --coverage-line --coverage-toggle --coverage-user" : "",
		fs_utils::escape_shell_arg(req.mdir.string()),
		fs_utils::escape_shell_arg(req.top_mod)
	);
	for (const auto &src : req.all_sources) {
		fs::path rel_src = fs::relative(src, req.root_path, ec);
		std::string src_arg = (!ec && !rel_src.empty()) ? rel_src.string() : fs::weakly_canonical(src, ec).string();
		ec.clear();
		compile_cmd += " " + fs_utils::escape_shell_arg(src_arg);
	}
	compile_cmd += " 2>&1";

	std::string compile_out;
	{
		scoped_eda_permit permit(*this);
		compile_out = fs_utils::execute_command_sync(compile_cmd, 120);
	}
	if (!fs::exists(req.sim_bin, ec)) {
		res.compile_failed = true;
		res.error_markdown = std::format("### Verilog Simulation Failed (Compilation Error)\n\n"
		                                 "- **Testbench**: `{}` (top module: `{}`)\n\n"
		                                 "#### Compiler Output\n```\n{}\n```\n",
		                                 req.display_tb, req.top_mod, compile_out);
	} else {
		write_build_signature(req.mdir, expected_sig);
	}
	// Strip ~222 MB of .fast.gch / .slow.gch + intermediate .o/.a/.cpp files now that V<top> is linked;
	// CCACHE_DIR preserves compiled objects across future rebuilds within its 2G cap.
	prune_intermediate_mdir_artifacts(req.mdir, req.sim_bin);

	// Publish result and remove from in-flight map
	{
		std::lock_guard<std::mutex> lk(in_flight_mutex_);
		in_flight_builds_.erase(mdir_key);
	}
	my_promise->set_value(res);
	return res;
}

void verilog_sim_build_cache::record_simulation_session(std::string_view project_root,
                                                        bool enable_coverage,
                                                        bool include_formal)
{
	// Never spawn background pre-compilation in one-shot CLI invocations
	if (fpga_project_settings::is_cli_mode() || project_root.empty()) {
		return;
	}
	std::error_code ec;
	fs::path canon_root = fs::weakly_canonical(project_root, ec);
	std::string root_str = ec ? std::string(project_root) : canon_root.string();

	std::lock_guard<std::mutex> lk(aot_mutex_);
	session_active_ = true;
	aot_project_root_ = std::move(root_str);
	aot_enable_coverage_ = enable_coverage;
	aot_include_formal_ = include_formal;
	// Cancel any pending/in-progress AOT burst from prior edits so AOT does not keep
	// compiling stale targets in the background after verilog_simulate() runs.
	pending_files_.clear();
	has_pending_work_ = false;
	++edit_generation_;
	if (!worker_started_) {
		worker_started_ = true;
		worker_thread_ = std::jthread([this](std::stop_token st) { worker_loop(st); });
	}
}

void verilog_sim_build_cache::begin_simulate_call() noexcept
{
	active_simulate_calls_.fetch_add(1, std::memory_order_acq_rel);
}

void verilog_sim_build_cache::end_simulate_call() noexcept
{
	active_simulate_calls_.fetch_sub(1, std::memory_order_acq_rel);
}

void verilog_sim_build_cache::on_file_changed(std::string_view filepath)
{
	if (filepath.empty() || !is_verilog_source_extension(filepath)) {
		return;
	}
	std::error_code ec;
	fs::path canon_file = fs::weakly_canonical(filepath, ec);
	std::string file_str = ec ? std::string(filepath) : canon_file.string();

	std::string root_copy;
	{
		std::lock_guard<std::mutex> lk(aot_mutex_);
		if (!session_active_ || aot_project_root_.empty()) {
			return;
		}
		if (!file_str.starts_with(aot_project_root_)) {
			return;
		}
		root_copy = aot_project_root_;
		pending_files_.insert(file_str);
		has_pending_work_ = true;
		++edit_generation_;
		debounce_deadline_ = std::chrono::steady_clock::now() + kDebounceDelay;
	}
	// Fail-safe: immediately delete stale V<top> executables and .turbostar_build.sig
	// for any testbench affected by this edit so a stale binary can never be executed.
	invalidate_affected_binaries(root_copy, file_str);
	worker_cv_.notify_one();
}

void verilog_sim_build_cache::shutdown()
{
	std::jthread to_join;
	{
		std::lock_guard<std::mutex> lk(aot_mutex_);
		session_active_ = false;
		has_pending_work_ = false;
		pending_files_.clear();
		if (worker_started_) {
			worker_thread_.request_stop();
			to_join = std::move(worker_thread_);
			worker_started_ = false;
		}
	}
	worker_cv_.notify_all();
	if (to_join.joinable()) {
		to_join.join();
	}
}

bool verilog_sim_build_cache::is_aot_session_active() const noexcept
{
	std::lock_guard<std::mutex> lk(aot_mutex_);
	return session_active_;
}

void verilog_sim_build_cache::reset_session_for_testing()
{
	shutdown();
}

void verilog_sim_build_cache::worker_loop(std::stop_token stoken)
{
	while (!stoken.stop_requested()) {
		std::string project_root;
		std::set<std::string> changed_files;
		bool enable_cov = false;
		bool include_formal = true;
		uint64_t gen = 0;

		{
			std::unique_lock<std::mutex> lk(aot_mutex_);
			worker_cv_.wait(lk, stoken, [&]() { return has_pending_work_; });
			if (stoken.stop_requested()) {
				break;
			}
			// Wait until debounce window expires with no further edits
			while (!stoken.stop_requested() && has_pending_work_) {
				auto now = std::chrono::steady_clock::now();
				if (now >= debounce_deadline_) {
					break;
				}
				worker_cv_.wait_until(lk, stoken, debounce_deadline_, [&]() {
					return !has_pending_work_ || std::chrono::steady_clock::now() >= debounce_deadline_;
				});
			}
			if (stoken.stop_requested() || !has_pending_work_ || !session_active_) {
				continue;
			}
			project_root = aot_project_root_;
			changed_files = std::move(pending_files_);
			pending_files_.clear();
			has_pending_work_ = false;
			enable_cov = aot_enable_coverage_;
			include_formal = aot_include_formal_;
			gen = edit_generation_;
		}

		run_aot_precompile_batch(project_root, changed_files, enable_cov, include_formal, gen, stoken);
	}
}

void verilog_sim_build_cache::invalidate_affected_binaries(const std::string &project_root,
                                                           const std::string &changed_file)
{
	std::error_code ec;
	fs::path root_path = fs::weakly_canonical(project_root, ec);
	if (ec) {
		root_path = project_root;
		ec.clear();
	}

	std::vector<testbench_entry> configured_tbs;
	std::string cfg_path, cfg_err;
	fpga_project_settings::load_testbenches(project_root, configured_tbs, cfg_path, cfg_err);
	if (configured_tbs.empty()) {
		return;
	}

	fpga_target_config fpga_cfg;
	fpga_project_settings::load_from_project(project_root, fpga_cfg, cfg_err);
	fs::path scan_root = fpga_cfg.sv_path.empty()
		? (root_path / fpga_project_settings::detect_sv_path(project_root))
		: (root_path / fpga_cfg.sv_path);
	if (!fs::exists(scan_root, ec)) {
		scan_root = root_path;
		ec.clear();
	}
	std::vector<std::string> rtl_files = verilog_module_scanner::find_synthesizable_files(scan_root);

	fs::path changed_canon = fs::weakly_canonical(changed_file, ec);
	std::string changed_str = ec ? changed_file : changed_canon.string();
	ec.clear();

	std::string ext = fs::path(changed_str).extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	const bool is_header = (ext == ".svh" || ext == ".vh");

	std::set<std::string> changed_mods;
	if (!is_header) {
		std::vector<std::string> single = {changed_str};
		for (const auto &m : verilog_module_scanner::discover_modules_in_files(single)) {
			changed_mods.insert(m.module_name);
		}
		for (const auto &bt : extract_bind_targets(changed_str)) {
			changed_mods.insert(bt);
		}
	}

	for (const auto &entry : configured_tbs) {
		fs::path tb_abs = fs::weakly_canonical(root_path / entry.testbench, ec);
		if (ec || !fs::is_regular_file(tb_abs, ec)) {
			ec.clear();
			continue;
		}
		bool affected = is_header || (tb_abs.string() == changed_str);
		if (!affected && !changed_mods.empty()) {
			for (const auto &m : entry.modules) {
				if (changed_mods.contains(m)) {
					affected = true;
					break;
				}
				auto hier = verilog_module_scanner::discover_submodule_hierarchy(m, rtl_files);
				for (const auto &hm : hier) {
					if (changed_mods.contains(hm)) {
						affected = true;
						break;
					}
				}
				if (affected) {
					break;
				}
			}
		}
		if (!affected) {
			continue;
		}

		std::vector<std::string> single_tb = {tb_abs.string()};
		auto tb_mods = verilog_module_scanner::discover_modules_in_files(single_tb);
		if (tb_mods.empty()) {
			continue;
		}
		const std::string &top_mod = tb_mods.front().module_name;

		// Remove stale binary + signature across all (coverage x formal) --Mdir variants
		for (bool cov : {false, true}) {
			for (bool frm : {false, true}) {
				fs::path mdir = compute_mdir_path(root_path, entry.testbench, top_mod, 0, cov, frm);
				std::string mdir_key = fs::weakly_canonical(mdir, ec).string();
				if (ec || mdir_key.empty()) {
					mdir_key = mdir.string();
					ec.clear();
				}
				{
					std::lock_guard<std::mutex> lk(in_flight_mutex_);
					if (in_flight_builds_.contains(mdir_key)) {
						continue;
					}
				}
				fs::remove(mdir / kSigFilename, ec);
				ec.clear();
				fs::remove(mdir / std::format("V{}", top_mod), ec);
				ec.clear();
			}
		}
	}
}

void verilog_sim_build_cache::run_aot_precompile_batch(const std::string &project_root,
                                                       const std::set<std::string> &changed_files,
                                                       bool enable_coverage,
                                                       bool include_formal,
                                                       uint64_t expected_generation,
                                                       std::stop_token stoken)
{
	if (changed_files.empty() || stoken.stop_requested()) {
		return;
	}
	std::error_code ec;
	fs::path root_path = fs::weakly_canonical(project_root, ec);
	if (ec) {
		root_path = project_root;
		ec.clear();
	}

	// Load project testbench configuration (only pre-compile `quick` testbenches)
	std::vector<testbench_entry> configured_tbs;
	std::string cfg_path, cfg_err;
	fpga_project_settings::load_testbenches(project_root, configured_tbs, cfg_path, cfg_err);
	if (configured_tbs.empty()) {
		return;
	}

	fpga_target_config fpga_cfg;
	fpga_project_settings::load_from_project(project_root, fpga_cfg, cfg_err);
	fs::path scan_root = fpga_cfg.sv_path.empty()
		? (root_path / fpga_project_settings::detect_sv_path(project_root))
		: (root_path / fpga_cfg.sv_path);
	if (!fs::exists(scan_root, ec)) {
		scan_root = root_path;
		ec.clear();
	}

	std::vector<std::string> rtl_files = verilog_module_scanner::find_synthesizable_files(scan_root);
	std::vector<std::string> formal_files = include_formal
		? verilog_module_scanner::find_formal_files(root_path)
		: std::vector<std::string>{};

	// Determine which modules were modified by changed_files
	std::set<std::string> changed_mods;
	std::set<std::string> changed_canon;
	for (const auto &cf : changed_files) {
		fs::path cp = fs::weakly_canonical(cf, ec);
		std::string cstr = ec ? cf : cp.string();
		ec.clear();
		changed_canon.insert(cstr);

		std::vector<std::string> single = {cstr};
		auto mods = verilog_module_scanner::discover_modules_in_files(single);
		for (const auto &m : mods) {
			changed_mods.insert(m.module_name);
		}
		for (const auto &bt : extract_bind_targets(cstr)) {
			changed_mods.insert(bt);
		}
	}

	// Collect all affected `quick` testbenches (prioritizing direct/leaf testbenches ahead of composite ones)
	std::vector<testbench_entry> direct_targets;
	std::vector<testbench_entry> transitive_targets;
	for (const auto &entry : configured_tbs) {
		if (fpga_project_settings::normalize_test_level(entry.level) != "quick") {
			continue;
		}
		fs::path tb_abs = fs::weakly_canonical(root_path / entry.testbench, ec);
		if (ec || !fs::is_regular_file(tb_abs, ec)) {
			ec.clear();
			continue;
		}
		bool direct_hit = changed_canon.contains(tb_abs.string());
		bool transitive_hit = false;
		if (!direct_hit && !changed_mods.empty()) {
			for (const auto &m : entry.modules) {
				if (changed_mods.contains(m)) {
					direct_hit = true;
					break;
				}
				auto hier = verilog_module_scanner::discover_submodule_hierarchy(m, rtl_files);
				for (const auto &hm : hier) {
					if (changed_mods.contains(hm)) {
						transitive_hit = true;
						break;
					}
				}
			}
		}
		if (direct_hit) {
			direct_targets.push_back(entry);
		} else if (transitive_hit) {
			transitive_targets.push_back(entry);
		}
	}

	std::vector<testbench_entry> targets;
	targets.reserve(direct_targets.size() + transitive_targets.size());
	targets.insert(targets.end(), direct_targets.begin(), direct_targets.end());
	targets.insert(targets.end(), transitive_targets.begin(), transitive_targets.end());

	if (targets.empty()) {
		return;
	}

	// Rate-limit AOT compiles to half of available CPU cores, running 1 testbench at a time
	unsigned int hw_threads = std::thread::hardware_concurrency();
	size_t half_cores = std::max<size_t>(1, (hw_threads == 0 ? 4u : hw_threads) / 2);

	for (const auto &tb : targets) {
		if (stoken.stop_requested() || active_simulate_calls_.load(std::memory_order_acquire) > 0) {
			return;
		}
		{
			std::lock_guard<std::mutex> lk(aot_mutex_);
			if (edit_generation_ != expected_generation || !session_active_) {
				return;
			}
		}

		fs::path tb_abs = fs::weakly_canonical(root_path / tb.testbench, ec);
		if (ec) {
			ec.clear();
			continue;
		}
		std::vector<std::string> single_tb = {tb_abs.string()};
		auto tb_mods = verilog_module_scanner::discover_modules_in_files(single_tb);
		if (tb_mods.empty()) {
			continue;
		}

		sim_compile_request req;
		req.root_path = root_path;
		req.project_root = project_root;
		req.display_tb = tb.testbench;
		req.top_mod = tb_mods.front().module_name;
		req.all_sources = collect_tb_sources(tb_abs.string(), rtl_files, formal_files);
		req.has_formal = !formal_files.empty();
		req.enable_coverage = enable_coverage;
		req.mdir = compute_mdir_path(root_path, tb.testbench, req.top_mod, 0, enable_coverage, req.has_formal);
		req.sim_bin = req.mdir / std::format("V{}", req.top_mod);
		req.build_jobs = half_cores;

		(void)compile_or_join(req);
	}
}

} // namespace verilog
