#include "virtual_file_system.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace agentlib {

virtual_file_system::mmap_handle::~mmap_handle() {
    if (data && data != MAP_FAILED && size > 0) {
        munmap(data, size);
    }
}

bool virtual_file_system::mount_file(const std::string& uri, const std::string& disk_path) {
    int fd = open(disk_path.c_str(), O_RDONLY);
    if (fd < 0) {
        return false;
    }

    struct stat sb;
    if (fstat(fd, &sb) < 0) {
        close(fd);
        return false;
    }

    if (sb.st_size == 0) {
        // Handle empty file. mmap might fail or return weird stuff for 0 bytes.
        close(fd);
        auto handle = std::make_unique<mmap_handle>();
        handle->size = 0;
        mounts_[uri] = std::move(handle);
        return true;
    }

    void* mapped = mmap(nullptr, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd); // POSIX guarantees mapped memory remains valid after close

    if (mapped == MAP_FAILED) {
        return false;
    }

    auto handle = std::make_unique<mmap_handle>();
    handle->data = mapped;
    handle->size = sb.st_size;
    
    mounts_[uri] = std::move(handle);
    return true;
}

void virtual_file_system::unmount_file(const std::string& uri) {
    mounts_.erase(uri);
}

void virtual_file_system::unmount_prefix(const std::string& prefix) {
    auto it = mounts_.lower_bound(prefix);
    while (it != mounts_.end() && it->first.starts_with(prefix)) {
        it = mounts_.erase(it);
    }
}

bool virtual_file_system::exists(const std::string& uri) const {
    return mounts_.contains(uri);
}

std::optional<std::string_view> virtual_file_system::read_file(const std::string& uri) const {
    auto it = mounts_.find(uri);
    if (it != mounts_.end()) {
        const auto& handle = it->second;
        if (handle->size == 0) {
            return std::string_view("");
        }
        return std::string_view(static_cast<const char*>(handle->data), handle->size);
    }
    return std::nullopt;
}

std::optional<vfs_file_info> virtual_file_system::get_file_info(const std::string& uri) const {
    auto it = mounts_.find(uri);
    if (it != mounts_.end()) {
        return vfs_file_info{uri, it->second->size};
    }
    return std::nullopt;
}

std::vector<vfs_file_info> virtual_file_system::list_directory(const std::string& prefix) const {
    std::vector<vfs_file_info> results;
    auto it = mounts_.lower_bound(prefix);
    while (it != mounts_.end() && it->first.starts_with(prefix)) {
        results.push_back({it->first, it->second->size});
        ++it;
    }
    return results;
}

} // namespace agentlib
