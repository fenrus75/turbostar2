#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include "virtual_file_system.h"

namespace agentlib
{

enum class access_type { read, write };

class file_security_manager
{
      public:
	/**
	 * @brief Constructs a new file security manager with standard hardcoded ignore patterns.
	 */
	file_security_manager();

	/**
	 * @brief Associates a virtual file system (VFS) with the security manager.
	 */
	void set_vfs(virtual_file_system *vfs)
	{
		vfs_ = vfs;
	}

	/**
	 * @brief Gets the associated virtual file system.
	 */
	virtual_file_system *get_vfs() const
	{
		return vfs_;
	}

	/**
	 * @brief Sets the working directory used to resolve relative paths.
	 * @param cwd The path to the working directory. Must exist and be a directory.
	 * @throws std::invalid_argument if the path does not exist or is not a directory.
	 */
	void set_working_directory(const std::filesystem::path &cwd);

	/**
	 * @brief Gets the current working directory.
	 */
	std::filesystem::path get_working_directory() const;

	/**
	 * @brief Configures a directory root where all operations matching max_permission or below are allowed.
	 */
	void add_allowed_root(const std::filesystem::path &root, access_type max_permission);

	/**
	 * @brief Configures a specific file where operations matching max_permission or below are allowed.
	 */
	void add_allowed_file(const std::filesystem::path &file, access_type max_permission);

	/**
	 * @brief Adds a custom ignore pattern.
	 */
	void add_ignore_pattern(const std::string &pattern);

	/**
	 * @brief Loads and parses ignore patterns from a file (e.g., .agentignore).
	 */
	void load_ignore_file(const std::filesystem::path &ignore_file_path);

	/**
	 * @brief The core validation method.
	 * Evaluates whether requested path access matches security policies.
	 * @param requested_path Arbitrary path (relative or absolute).
	 * @param requested_perm The requested access type (read or write).
	 * @param out_resolved_path Output parameter containing the fully canonicalized absolute path.
	 * @param out_error Output parameter containing details in case of failure.
	 * @return true if access is allowed, false otherwise.
	 */
	bool validate_access(const std::string &requested_path, access_type requested_perm, std::string &out_resolved_path,
			     std::string &out_error) const;

      private:
	virtual_file_system *vfs_{nullptr};

	struct allowed_path {
		std::filesystem::path path;
		access_type perm;
		bool is_directory;
	};

	std::filesystem::path cwd_;
	std::vector<allowed_path> allowlist_;
	std::vector<std::string> ignore_patterns_;

	bool is_ignored(const std::filesystem::path &path) const;
};

} // namespace agentlib
