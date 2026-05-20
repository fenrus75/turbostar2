#pragma once

#include <string>
#include <map>
#include <vector>
#include <memory>
#include <string_view>
#include <optional>
#include <cstdint>

namespace agentlib {

struct vfs_file_info {
    std::string uri;
    size_t size;
    char type; // 'F', 'D', 'L'
    size_t size_in_lines;
};

/**
 * @brief Memory-Mapped Virtual File System.
 * Securely maps disk files to virtual URIs (e.g. skills://) using mmap.
 * Provides zero-copy read-only access to tools.
 */
class virtual_file_system {
public:
    virtual_file_system() = default;
    ~virtual_file_system() = default;

    // Delete copy/move since it manages mmap resources
    virtual_file_system(const virtual_file_system&) = delete;
    virtual_file_system& operator=(const virtual_file_system&) = delete;

    /**
     * @brief Maps a physical file on disk to a virtual URI using mmap.
     * @param uri The virtual URI (e.g. "skills://myskill/foo.md")
     * @param disk_path The absolute path on disk to map.
     * @return true if successful, false if file doesn't exist or mmap fails.
     */
    bool mount_file(const std::string& uri, const std::string& disk_path);

    /**
     * @brief Unmounts a specific URI.
     */
    void unmount_file(const std::string& uri);

    /**
     * @brief Unmounts all URIs starting with a specific prefix.
     */
    void unmount_prefix(const std::string& prefix);

    /**
     * @brief Checks if a URI exists in the VFS.
     */
    bool exists(const std::string& uri) const;

    /**
     * @brief Returns a string_view of the file content.
     * The view is valid as long as the file remains mounted.
     * @return std::nullopt if the URI is not found.
     */
    std::optional<std::string_view> read_file(const std::string& uri) const;

    /**
     * @brief Returns basic information about a file, like its size.
     */
    std::optional<vfs_file_info> get_file_info(const std::string& uri) const;

    /**
     * @brief Returns a list of all URIs that start with the given directory prefix.
     * This supports tools like fs_list_dir.
     * Example prefix: "skills://myskill/"
     */
    std::vector<vfs_file_info> list_directory(const std::string& prefix) const;

private:
    struct mmap_handle {
        void* data{nullptr};
        size_t size{0};
        char type{'F'};
        size_t size_in_lines{0};

        ~mmap_handle();
    };

    std::map<std::string, std::unique_ptr<mmap_handle>> mounts_;
};

} // namespace agentlib
