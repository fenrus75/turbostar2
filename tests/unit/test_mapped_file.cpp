// Tested source file: src/mapped_file.cpp
#include "test_watchdog.h"
#include "mapped_file.h"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>

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

	std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "turbostar_test_mapped_file";
	std::filesystem::create_directories(temp_dir);

	// 1. Test normal non-empty file mapping, span, string_view, implicit conversions, and indexing
	{
		std::filesystem::path normal_file = temp_dir / "sample.txt";
		const std::string expected = "Hello from mapped_file!\nSecond line.\n";
		{
			std::ofstream ofs(normal_file, std::ios::binary);
			assert(ofs.is_open());
			ofs.write(expected.data(), static_cast<std::streamsize>(expected.size()));
		}

		mapped_file mf;
		assert(!mf.is_open());
		assert(mf.empty());

		std::string err;
		bool ok = mf.open(normal_file.string(), err);
		assert(ok && err.empty());
		assert(mf.is_open());
		assert(!mf.empty());
		assert(mf.size() == expected.size());
		assert(mf.string_view() == expected);
		assert(mf.bytes().size() == expected.size());
		assert(mf[0] == static_cast<uint8_t>('H'));
		assert(mf[expected.size() - 1] == static_cast<uint8_t>('\n'));

		// Implicit conversions to std::span<const uint8_t> and std::string_view
		assert(count_bytes(mf) == expected.size());
		assert(count_chars(mf) == expected.size());

		// Move construction and move assignment
		mapped_file mf_moved(std::move(mf));
		assert(mf_moved.is_open());
		assert(mf_moved.string_view() == expected);

		mapped_file mf_assigned;
		mf_assigned = std::move(mf_moved);
		assert(mf_assigned.is_open());
		assert(mf_assigned.string_view() == expected);

		mf_assigned.close();
		assert(!mf_assigned.is_open());
		assert(mf_assigned.empty());
	}

	// 2. Test 0-byte regular file
	{
		std::filesystem::path empty_file = temp_dir / "empty.bin";
		{
			std::ofstream ofs(empty_file, std::ios::binary);
			assert(ofs.is_open());
		}

		mapped_file mf;
		std::string err;
		bool ok = mf.open(empty_file.string(), err);
		assert(ok && err.empty());
		assert(mf.is_open());
		assert(mf.empty());
		assert(mf.size() == 0);
		assert(mf.bytes().empty());
		assert(mf.string_view().empty());
	}

	// 3. Test nonexistent file and directory rejection
	{
		mapped_file mf;
		std::string err;
		assert(!mf.open((temp_dir / "does_not_exist.bin").string(), err));
		assert(!err.empty());
		assert(!mf.is_open());

		err.clear();
		assert(!mf.open(temp_dir.string(), err));
		assert(err.find("regular file") != std::string::npos);
		assert(!mf.is_open());
	}

	std::filesystem::remove_all(temp_dir);
	std::cout << "test_mapped_file passed!\n";
	return 0;
}
