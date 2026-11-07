#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/**
 * RAII read-only memory-mapped file abstraction with transparent heap fallback.
 *
 * Goals & Constraints:
 * - Maps regular files into the process virtual address space using `mmap(PROT_READ, MAP_PRIVATE)`
 *   so large files (such as unstripped ELF binaries or archives) do not require heap allocation
 *   or eager full-file disk reads.
 * - Closes the underlying file descriptor immediately after `mmap` succeeds so no file descriptors
 *   remain held while the mapping is active.
 * - Transparently falls back to a heap buffer (`std::vector<uint8_t>`) populated via `::read`
 *   if `mmap` is unsupported by the underlying filesystem or fails.
 * - Handles 0-byte regular files cleanly (`is_open() == true`, `empty() == true`, `size() == 0`).
 * - Deletes rvalue conversion operators (`const &&`) to prevent dangling `std::span` or
 *   `std::string_view` bindings to temporary `mapped_file` objects at compile time.
 */
class mapped_file
{
      public:
	mapped_file() noexcept = default;
	~mapped_file() noexcept;

	mapped_file(const mapped_file &) = delete;
	mapped_file &operator=(const mapped_file &) = delete;

	mapped_file(mapped_file &&other) noexcept;
	mapped_file &operator=(mapped_file &&other) noexcept;

	// Opens and maps `untrusted_path` for read-only access.
	// Validates that the opened descriptor refers to a regular file (`S_ISREG`).
	bool open(/* untrusted */ std::string_view path, std::string &out_error) noexcept;

	// Releases any active memory mapping or fallback buffer.
	void close() noexcept;

	[[nodiscard]] constexpr bool is_open() const noexcept { return is_open_; }
	[[nodiscard]] constexpr bool is_mapped() const noexcept { return mapped_ptr_ != nullptr; }
	[[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }
	[[nodiscard]] constexpr size_t size() const noexcept { return size_; }
	[[nodiscard]] constexpr const uint8_t *data() const noexcept { return data_; }

	[[nodiscard]] constexpr uint8_t operator[](size_t index) const noexcept { return data_[index]; }

	[[nodiscard]] constexpr std::span<const uint8_t> bytes() const & noexcept { return {data_, size_}; }
	[[nodiscard]] constexpr std::string_view string_view() const & noexcept
	{
		return {reinterpret_cast<const char *>(data_), size_};
	}

	constexpr operator std::span<const uint8_t>() const & noexcept { return bytes(); }
	constexpr operator std::string_view() const & noexcept { return string_view(); }

	// Prevent binding non-owning views to a temporary mapped_file
	void bytes() const && = delete;
	void string_view() const && = delete;
	operator std::span<const uint8_t>() const && = delete;
	operator std::string_view() const && = delete;

      private:
	const uint8_t *data_{nullptr};
	void *mapped_ptr_{nullptr};
	std::vector<uint8_t> fallback_buf_;
	size_t size_{0};
	bool is_open_{false};
};
