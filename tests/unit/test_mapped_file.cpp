// Tested source file: src/mapped_file.cpp
#include "test_watchdog.h"
#include "mapped_file.h"
#include <cassert>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

static size_t count_bytes(std::span<const uint8_t> sp)
{
	return sp.size();
}

static size_t count_chars(std::string_view sv)
{
	return sv.size();
}

int main()
{
	test_watchdog::setup_watchdog(30);
	mapped_file::clear_cache();

	std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "turbostar_test_mapped_file";
	std::filesystem::remove_all(temp_dir);
	std::filesystem::create_directories(temp_dir);

	// 1. Test normal non-empty file mapping, span, string_view, lines(), is_binary(), and moves
	{
		std::filesystem::path normal_file = temp_dir / "sample.txt";
		const std::string expected = "Hello from mapped_file!\r\nSecond line.\nThird line without newline";
		{
			std::ofstream ofs(normal_file, std::ios::binary);
			assert(ofs.is_open());
			ofs.write(expected.data(), static_cast<std::streamsize>(expected.size()));
		}

		mapped_file mf;
		assert(!mf.is_open());
		assert(mf.empty());

		std::string err;
		bool ok = mf.open(normal_file.string(), err, /*populate=*/true);
		assert(ok && err.empty());
		assert(mf.is_open());
		assert(!mf.empty());
		assert(!mf.is_binary());
		assert(mf.size() == expected.size());
		assert(mf.string_view() == expected);
		assert(mf.bytes().size() == expected.size());
		assert(mf[0] == static_cast<uint8_t>('H'));

		// Verify zero-copy lines() with \r\n stripping and no-trailing-newline handling
		auto lines = mf.lines();
		assert(lines.size() == 3);
		assert(lines[0] == "Hello from mapped_file!");
		assert(lines[1] == "Second line.");
		assert(lines[2] == "Third line without newline");

		// Implicit conversions to std::span<const uint8_t> and std::string_view
		assert(count_bytes(mf) == expected.size());
		assert(count_chars(mf) == expected.size());

		// Move construction and move assignment
		mapped_file mf_moved(std::move(mf));
		assert(mf_moved.is_open());
		assert(mf_moved.string_view() == expected);
		assert(mf_moved.lines().size() == 3);

		mapped_file mf_assigned;
		mf_assigned = std::move(mf_moved);
		assert(mf_assigned.is_open());
		assert(mf_assigned.string_view() == expected);
		assert(mf_assigned.lines().size() == 3);

		mf_assigned.close();
		assert(!mf_assigned.is_open());
		assert(mf_assigned.empty());
	}

	// 2. Test binary detection (NUL byte vs control byte)
	{
		std::filesystem::path nul_file = temp_dir / "with_nul.bin";
		{
			std::ofstream ofs(nul_file, std::ios::binary);
			const char data[] = {'a', 'b', '\0', 'c'};
			ofs.write(data, sizeof(data));
		}
		mapped_file mf_nul;
		std::string err;
		assert(mf_nul.open(nul_file.string(), err));
		assert(mf_nul.is_binary(/*treat_null_as_binary=*/true));
		assert(!mf_nul.is_binary(/*treat_null_as_binary=*/false));

		std::filesystem::path ctrl_file = temp_dir / "with_ctrl.bin";
		{
			std::ofstream ofs(ctrl_file, std::ios::binary);
			const char data[] = {'a', '\x02', 'c'};
			ofs.write(data, sizeof(data));
		}
		mapped_file mf_ctrl;
		assert(mf_ctrl.open(ctrl_file.string(), err));
		assert(mf_ctrl.is_binary(/*treat_null_as_binary=*/true));
		assert(mf_ctrl.is_binary(/*treat_null_as_binary=*/false));
	}

	// 3. Test 0-byte regular file
	{
		std::filesystem::path empty_file = temp_dir / "empty.bin";
		{
			std::ofstream ofs(empty_file, std::ios::binary);
			assert(ofs.is_open());
		}

		mapped_file mf;
		std::string err;
		bool ok = mf.open(empty_file.string(), err, /*populate=*/true);
		assert(ok && err.empty());
		assert(mf.is_open());
		assert(mf.empty());
		assert(mf.size() == 0);
		assert(mf.bytes().empty());
		assert(mf.string_view().empty());
		assert(mf.lines().empty());
	}

	// 4. Test refcounted LRU cache, mtime/size invalidation, notify_changed rewarm, and prefetch
	{
		mapped_file::clear_cache();
		std::filesystem::path cached_path = temp_dir / "cached.txt";
		{
			std::ofstream ofs(cached_path, std::ios::binary);
			ofs << "line 1\nline 2\n";
		}

		std::string err;
		auto c1 = mapped_file::get_cached(cached_path.string(), err);
		assert(c1 != nullptr);
		assert(c1->lines().size() == 2);

		auto stats1 = mapped_file::get_cache_stats();
		assert(stats1.misses == 1);
		assert(stats1.hits == 0);

		auto c2 = mapped_file::get_cached(cached_path.string(), err);
		assert(c2 == c1);
		auto stats2 = mapped_file::get_cache_stats();
		assert(stats2.hits == 1);

		// Modify file size on disk without calling notify_changed -> stat validation must evict stale entry
		{
			std::ofstream ofs(cached_path, std::ios::binary | std::ios::trunc);
			ofs << "line 1\nline 2\nline 3\n";
		}
		auto c3 = mapped_file::get_cached(cached_path.string(), err);
		assert(c3 != nullptr && c3 != c1);
		assert(c3->lines().size() == 3);
		assert(c3->line_range_view(2, 3) == "line 2\nline 3");
		assert(c3->line_range_view(1, 1) == "line 1");
		assert(c3->line_range_view(4, 5).empty());
		// Old shared_ptr `c1` remains valid and readable while held!
		assert(c1->lines().size() == 2);

		auto stats3 = mapped_file::get_cache_stats();
		assert(stats3.stale_evictions == 1);

		// Test notify_changed proactive invalidation + rewarm
		{
			std::ofstream ofs(cached_path, std::ios::binary | std::ios::trunc);
			ofs << "rewarmed 1\nrewarmed 2\nrewarmed 3\nrewarmed 4\n";
		}
		mapped_file::notify_changed(cached_path.string());
		auto stats4 = mapped_file::get_cache_stats();
		assert(stats4.notify_invalidations == 1);
		assert(stats4.notify_rewarms == 1);

		// Test bounded batch prefetching
		std::vector<std::string> batch_paths;
		for (int i = 0; i < 8; ++i) {
			auto p = temp_dir / ("pf_" + std::to_string(i) + ".txt");
			std::ofstream ofs(p, std::ios::binary);
			ofs << "prefetched file " << i << "\nsecond line\n";
			batch_paths.push_back(p.string());
		}
		mapped_file::prefetch(batch_paths);

		// Wait briefly for the background worker to finish prefetching the 8 small files
		for (int attempt = 0; attempt < 100; ++attempt) {
			if (mapped_file::get_cache_stats().prefetch_completed >= 8) {
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}

		for (const auto &p : batch_paths) {
			auto pf = mapped_file::get_cached(p, err);
			assert(pf != nullptr);
			assert(pf->lines().size() == 2);
		}

		auto stats5 = mapped_file::get_cache_stats();
		assert(stats5.prefetch_completed >= 8);
		assert(stats5.prefetch_hits >= 8);
		assert(stats5.unused_prefetch_evictions == 0);
	}

	mapped_file::shutdown_cache();
	std::filesystem::remove_all(temp_dir);
	std::cout << "test_mapped_file passed!\n";
	return 0;
}
