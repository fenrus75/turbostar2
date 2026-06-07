#pragma once
#include <filesystem>
#include <string>
#include "../../src/fs_utils.h"
#include "../../src/project_manager.h"

class temp_git_repo
{
      public:
	inline temp_git_repo(const std::string &test_name)
	{
		std::string project_root = project_manager::get_instance().get_project_root();
		path_ = std::filesystem::path(project_root) / "tests" / "unit" / ("tmp_git_" + test_name);

		// Clean up any stale directory first
		std::filesystem::remove_all(path_);
		std::filesystem::create_directories(path_);

		// Override project directory
		fs_utils::set_override_project_dir(path_.string());
		setenv("TURBOSTAR_PROJECT_ROOT", path_.string().c_str(), 1);

		// Initialize Git repo in the temp directory
		fs_utils::execute_command_sync("git init");
		fs_utils::execute_command_sync("git config user.name \"Test User\"");
		fs_utils::execute_command_sync("git config user.email \"test@example.com\"");
		fs_utils::execute_command_sync("touch dummy_initial.txt");
		fs_utils::execute_command_sync("git add dummy_initial.txt");
		fs_utils::execute_command_sync("git commit -m \"initial commit\"");
	}

	inline ~temp_git_repo()
	{
		fs_utils::set_override_project_dir("");
		unsetenv("TURBOSTAR_PROJECT_ROOT");
		std::filesystem::remove_all(path_);
	}

	inline std::string get_path() const
	{
		return path_.string();
	}

      private:
	std::filesystem::path path_;
};
