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
	mutable std::mutex mutex_;
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

	void register_provider(const std::string &scheme, std::shared_ptr<vfs_provider> provider);

      private:
	std::shared_ptr<vfs_provider> get_provider_for_uri(const std::string &uri) const;

	std::shared_ptr<memory_vfs_provider> default_provider_;
	std::map<std::string, std::shared_ptr<vfs_provider>> providers_;
};

} // namespace agentlib
