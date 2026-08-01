#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <mutex>

namespace httplib
{
class Client;
}

namespace agentlib
{

struct vfs_file_info {
	std::string uri;
	size_t size;
	char type; // 'F', 'D', 'L'
	size_t size_in_lines;
};

/**
 * @brief RAII managed virtual file content buffer
 */
class vfs_content_buffer
{
      public:
	virtual ~vfs_content_buffer() = default;
	virtual std::string_view view() const = 0;
};

using vfs_file_handle = std::shared_ptr<const vfs_content_buffer>;

struct mmap_handle {
	void *data{nullptr};
	size_t size{0};
	char type{'F'};
	size_t size_in_lines{0};

	~mmap_handle();
};

class mmap_content_buffer : public vfs_content_buffer
{
      public:
	explicit mmap_content_buffer(std::shared_ptr<mmap_handle> handle) : handle_(std::move(handle))
	{
	}
	std::string_view view() const override
	{
		if (!handle_ || handle_->size == 0)
			return "";
		return std::string_view(static_cast<const char *>(handle_->data), handle_->size);
	}

      private:
	std::shared_ptr<mmap_handle> handle_;
};

class string_content_buffer : public vfs_content_buffer
{
      public:
	explicit string_content_buffer(std::string data) : data_(std::move(data))
	{
	}
	std::string_view view() const override
	{
		return data_;
	}

      private:
	std::string data_;
};

/**
 * @brief RAII or handle-based interface for writing virtual files.
 */
class vfs_writer
{
      public:
	virtual ~vfs_writer() = default;

	/**
	 * @brief Write binary data chunk to the virtual file.
	 * @return true on success, false on error.
	 */
	virtual bool write(const void *data, size_t size) = 0;

	/**
	 * @brief Finalize and commit the write transaction.
	 * @return A human-readable success description with the final URI
	 *         or empty string on failure.
	 */
	virtual std::string close() = 0;
};

using vfs_write_handle = std::unique_ptr<vfs_writer>;

/*

# subclasses of vfs_provider

| subclass            | filename                           |
| ------------------- | ---------------------------------- |
| memory_vfs_provider | src/agentlib/virtual_file_system.h |
| github_vfs_provider | src/agentlib/virtual_file_system.h |
| file_vfs_provider   | src/agentlib/virtual_file_system.h |
| images_vfs_provider | src/agentlib/virtual_file_system.h |
| system_vfs_provider | src/vfs/system_vfs_provider.h      |

*/

/**
 * @brief Base class for scheme-specific virtual filesystem providers (e.g. github://)
 */
class vfs_provider
{
      public:
	virtual ~vfs_provider() = default;
	virtual bool exists(const std::string &uri) const = 0;
	virtual std::optional<vfs_file_handle> read_file(const std::string &uri) = 0;
	virtual std::optional<vfs_file_info> get_file_info(const std::string &uri) const = 0;
	virtual std::vector<vfs_file_info> list_directory(const std::string &prefix) const = 0;

	/**
	 * @brief Create a virtual file for streaming writes.
	 * @param uri The VFS URI to create.
	 * @return vfs_write_handle if supported/successful, std::nullopt otherwise.
	 */
	virtual std::optional<vfs_write_handle> create_file(const std::string &)
	{
		return std::nullopt; // Default: write unsupported
	}

	/**
	 * @brief Write the entire file contents in one go.
	 * @param uri The VFS URI to create.
	 * @param data Pointer to the memory buffer.
	 * @param size Size of the buffer in bytes.
	 * @return A success description containing the final URI, or empty string on failure.
	 */
	virtual std::string write_file(const std::string &uri, const void *data, size_t size)
	{
		auto handle = create_file(uri);
		if (!handle) {
			return "";
		}
		if (!(*handle)->write(data, size)) {
			return "";
		}
		return (*handle)->close();
	}

	virtual bool create_directory(const std::string &)
	{
		return false;
	}

	virtual bool is_local_path_available(const std::string &) const
	{
		return false;
	}

	virtual std::string get_local_path(const std::string &) const
	{
		return "";
	}
};

class memory_vfs_provider : public vfs_provider
{
      public:
	memory_vfs_provider() = default;
	~memory_vfs_provider() override = default;

	// Delete copy/move since it manages mmap resources
	memory_vfs_provider(const memory_vfs_provider &) = delete;
	memory_vfs_provider &operator=(const memory_vfs_provider &) = delete;

	bool mount_file(const std::string &uri, const std::string &disk_path);
	bool mount_buffer(const std::string &uri, const std::string &buffer);
	void unmount_file(const std::string &uri);
	void unmount_prefix(const std::string &prefix);

	bool exists(const std::string &uri) const override;
	std::optional<vfs_file_handle> read_file(const std::string &uri) override;
	std::optional<vfs_file_info> get_file_info(const std::string &uri) const override;
	std::vector<vfs_file_info> list_directory(const std::string &prefix) const override;

      private:
	std::map<std::string, std::shared_ptr<mmap_handle>> mounts_;
	void ensure_directories_exist(const std::string &file_uri);
};

class github_vfs_provider : public vfs_provider
{
      public:
	github_vfs_provider() = default;
	~github_vfs_provider() override = default;

	bool exists(const std::string &uri) const override;
	std::optional<vfs_file_handle> read_file(const std::string &uri) override;
	std::optional<vfs_file_info> get_file_info(const std::string &uri) const override;
	std::vector<vfs_file_info> list_directory(const std::string &prefix) const override;

      private:
	struct github_uri {
		std::string owner;
		std::string repo;
		std::string branch;
		std::string path;
		bool is_user_only{false};
		bool is_repo_root{false};
	};

	std::optional<github_uri> parse_uri(const std::string &uri) const;

	std::string http_get(const std::string &url, int &out_status) const;
	std::string get_default_branch(const std::string &owner, const std::string &repo) const;

	std::optional<vfs_file_info> get_file_info_unlocked(const std::string &uri) const;
	bool exists_unlocked(const std::string &uri) const;

	std::optional<std::string> cache_get(const std::string &key) const;
	void cache_put(const std::string &key, const std::string &data) const;
	void update_lru(const std::string &key) const;

	mutable std::map<std::string, std::string> file_cache_;
	mutable std::vector<std::string> file_lru_;
	mutable std::map<std::string, std::vector<vfs_file_info>> dir_cache_;
	mutable std::map<std::string, std::string> branch_cache_;
	/*
	 * mutex_ protects the internal GitHub VFS cache maps and LRU lists (file_cache_,
	 * file_lru_, dir_cache_, branch_cache_).
	 * Locking Rules:
	 * - Held briefly when querying or putting items into the VFS cache, or updating the LRU.
	 */
	mutable std::mutex mutex_;
};

class file_vfs_provider : public vfs_provider
{
      public:
	explicit file_vfs_provider(std::string scheme = "file", std::string root_dir = "")
	    : scheme_(std::move(scheme)), root_dir_(std::move(root_dir))
	{
	}
	~file_vfs_provider() override = default;

	bool exists(const std::string &uri) const override;
	std::optional<vfs_file_handle> read_file(const std::string &uri) override;
	std::optional<vfs_file_info> get_file_info(const std::string &uri) const override;
	std::vector<vfs_file_info> list_directory(const std::string &prefix) const override;
	std::optional<vfs_write_handle> create_file(const std::string &uri) override;
	bool create_directory(const std::string &uri) override;
	bool is_local_path_available(const std::string &uri) const override;
	std::string get_local_path(const std::string &uri) const override;

      private:
	std::string scheme_;
	std::string root_dir_;
	std::string parse_uri(const std::string &uri) const;
};

class images_vfs_provider : public vfs_provider
{
      public:
	images_vfs_provider() = default;
	~images_vfs_provider() override = default;

	bool exists(const std::string &uri) const override;
	std::optional<vfs_file_handle> read_file(const std::string &uri) override;
	std::optional<vfs_file_info> get_file_info(const std::string &uri) const override;
	std::vector<vfs_file_info> list_directory(const std::string &prefix) const override;
	std::optional<vfs_write_handle> create_file(const std::string &uri) override;
	bool is_local_path_available(const std::string &uri) const override;
	std::string get_local_path(const std::string &uri) const override;
};

/**
 * @brief Memory-Mapped Virtual File System coordinating scheme-specific providers.
 */
class virtual_file_system
{
      public:
	virtual_file_system();
	~virtual_file_system() = default;

	virtual_file_system(const virtual_file_system &) = delete;
	virtual_file_system &operator=(const virtual_file_system &) = delete;

	bool mount_file(const std::string &uri, const std::string &disk_path);
	bool mount_buffer(const std::string &uri, const std::string &buffer);
	void unmount_file(const std::string &uri);
	void unmount_prefix(const std::string &prefix);

	bool exists(const std::string &uri) const;
	std::optional<vfs_file_handle> read_file(const std::string &uri);
	std::optional<vfs_file_info> get_file_info(const std::string &uri) const;
	std::vector<vfs_file_info> list_directory(const std::string &prefix) const;

	std::optional<vfs_write_handle> create_file(const std::string &uri);
	std::string write_file(const std::string &uri, const void *data, size_t size);
	bool create_directory(const std::string &uri);
	bool is_local_path_available(const std::string &uri) const;
	std::string get_local_path(const std::string &uri) const;

	void register_provider(const std::string &scheme, std::shared_ptr<vfs_provider> provider);

      private:
	std::shared_ptr<vfs_provider> get_provider_for_uri(const std::string &uri) const;

	std::shared_ptr<memory_vfs_provider> default_provider_;
	std::map<std::string, std::shared_ptr<vfs_provider>> providers_;
};

} // namespace agentlib
