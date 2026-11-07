#include "mapped_file.h"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <format>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace
{
// Maximum file size allowed when falling back from mmap() to heap allocation (256 MB)
constexpr size_t k_max_fallback_read_size = 256ULL * 1024ULL * 1024ULL;
// Maximum virtual mapping size (4 GB) to guard against runaway mappings on corrupted filesystems
constexpr size_t k_max_mmap_size = 4ULL * 1024ULL * 1024ULL * 1024ULL;
} // namespace

mapped_file::~mapped_file() noexcept
{
	close();
}

mapped_file::mapped_file(mapped_file &&other) noexcept
    : data_(std::exchange(other.data_, nullptr)),
      mapped_ptr_(std::exchange(other.mapped_ptr_, nullptr)),
      fallback_buf_(std::move(other.fallback_buf_)),
      size_(std::exchange(other.size_, 0)),
      is_open_(std::exchange(other.is_open_, false))
{
	if (mapped_ptr_ == nullptr && !fallback_buf_.empty()) {
		data_ = fallback_buf_.data();
	}
}

mapped_file &mapped_file::operator=(mapped_file &&other) noexcept
{
	if (this != &other) {
		close();
		data_ = std::exchange(other.data_, nullptr);
		mapped_ptr_ = std::exchange(other.mapped_ptr_, nullptr);
		fallback_buf_ = std::move(other.fallback_buf_);
		size_ = std::exchange(other.size_, 0);
		is_open_ = std::exchange(other.is_open_, false);
		if (mapped_ptr_ == nullptr && !fallback_buf_.empty()) {
			data_ = fallback_buf_.data();
		}
	}
	return *this;
}

bool mapped_file::open(/* untrusted */ std::string_view path, std::string &out_error) noexcept
{
	close();

	if (path.empty()) {
		out_error = "File path is empty.";
		return false;
	}

	std::string path_str(path);
	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
	int fd = ::open(path_str.c_str(), O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		out_error = std::format("Failed to open file: {}", std::strerror(errno));
		return false;
	}

	struct stat st{};
	if (::fstat(fd, &st) != 0) {
		int err = errno;
		::close(fd);
		out_error = std::format("Failed to stat file: {}", std::strerror(err));
		return false;
	}

	if (!S_ISREG(st.st_mode)) {
		::close(fd);
		out_error = "Target path is not a regular file.";
		return false;
	}

	if (st.st_size < 0) {
		::close(fd);
		out_error = "File has invalid negative size.";
		return false;
	}

	const auto file_size = static_cast<size_t>(st.st_size);
	if (file_size == 0) {
		::close(fd);
		data_ = nullptr;
		size_ = 0;
		is_open_ = true;
		return true;
	}

	if (file_size > k_max_mmap_size) {
		::close(fd);
		out_error = "File is too large (>4GB) to map.";
		return false;
	}

	void *mapped = ::mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (mapped != MAP_FAILED) {
		::close(fd);
		mapped_ptr_ = mapped;
		data_ = static_cast<const uint8_t *>(mapped);
		size_ = file_size;
		is_open_ = true;
		return true;
	}

	// Transparent fallback: if mmap fails (e.g. FUSE/virtual filesystem or vm.max_map_count limit),
	// allocate a heap buffer and read the file contents via ::read().
	if (file_size > k_max_fallback_read_size) {
		int err = errno;
		::close(fd);
		out_error = std::format("mmap failed ({}) and file exceeds 256MB fallback read limit.", std::strerror(err));
		return false;
	}

	try {
		fallback_buf_.resize(file_size);
	} catch (...) {
		::close(fd);
		out_error = "Out of memory allocating fallback file buffer.";
		return false;
	}

	size_t total_read = 0;
	while (total_read < file_size) {
		ssize_t n = ::read(fd, fallback_buf_.data() + total_read, file_size - total_read);
		if (n < 0) {
			if (errno == EINTR) {
				continue;
			}
			int err = errno;
			::close(fd);
			fallback_buf_.clear();
			fallback_buf_.shrink_to_fit();
			out_error = std::format("Failed to read file contents: {}", std::strerror(err));
			return false;
		}
		if (n == 0) {
			break;
		}
		total_read += static_cast<size_t>(n);
	}
	::close(fd);

	fallback_buf_.resize(total_read);
	data_ = fallback_buf_.data();
	size_ = total_read;
	is_open_ = true;
	return true;
}

void mapped_file::close() noexcept
{
	if (mapped_ptr_ != nullptr) {
		::munmap(mapped_ptr_, size_);
		mapped_ptr_ = nullptr;
	}
	if (!fallback_buf_.empty()) {
		fallback_buf_.clear();
		fallback_buf_.shrink_to_fit();
	}
	data_ = nullptr;
	size_ = 0;
	is_open_ = false;
}
