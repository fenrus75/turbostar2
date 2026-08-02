#include "test_watchdog.h"
#include "project_template_manager.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

static std::string read_file_content(const std::filesystem::path &p)
{
	std::ifstream ifs(p, std::ios::binary);
	return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
}

int main()
{
	test_watchdog::setup_watchdog(30);

	std::cout << "Testing project_template_manager..." << std::endl;

	auto &mgr = turbostar::project_template_manager::get_instance();
	auto templates = mgr.get_available_templates();
	assert(templates.size() >= 6);

	test_watchdog::scoped_test_home guard("project_template_test");
	std::filesystem::path temp_dir = std::filesystem::path(guard.get_path()) / "test_new_project_output";
	if (std::filesystem::exists(temp_dir)) {
		std::filesystem::remove_all(temp_dir);
	}

	// 1. Test directory empty check
	assert(turbostar::project_template_manager::is_directory_empty(temp_dir));

	// 2. Instantiate Meson C++23 project
	{
		turbostar::project_create_options opts;
		opts.project_name = "demo_app";
		opts.executable_name = "demo_app";
		opts.language = "C++";
		opts.buildsystem = "Meson";
		opts.language_standard = "C++23";
		opts.target_directory = temp_dir / "demo_cpp23";
		opts.init_git = true;

		std::string err;
		bool ok = mgr.create_project(opts, err);
		assert(ok);
		assert(err.empty());

		assert(std::filesystem::exists(opts.target_directory / "meson.build"));
		assert(std::filesystem::exists(opts.target_directory / "src/main.cpp"));
		assert(std::filesystem::exists(opts.target_directory / "AGENTS.md"));
		assert(std::filesystem::exists(opts.target_directory / ".gitignore"));
		assert(std::filesystem::exists(opts.target_directory / ".git"));

		std::string main_cpp = read_file_content(opts.target_directory / "src/main.cpp");
		assert(main_cpp.find("Hello from demo_app!") != std::string::npos);
		assert(main_cpp.find("std::println") != std::string::npos);

		std::string meson_build = read_file_content(opts.target_directory / "meson.build");
		assert(meson_build.find("cpp_std=C++23") != std::string::npos);
	}

	// 3. Instantiate Meson C++17 project (verifying .C++17 version override)
	{
		turbostar::project_create_options opts;
		opts.project_name = "demo_legacy";
		opts.executable_name = "demo_legacy";
		opts.language = "C++";
		opts.buildsystem = "Meson";
		opts.language_standard = "C++17";
		opts.target_directory = temp_dir / "demo_cpp17";
		opts.init_git = false;

		std::string err;
		bool ok = mgr.create_project(opts, err);
		assert(ok);
		assert(err.empty());

		assert(std::filesystem::exists(opts.target_directory / "src/main.cpp"));
		std::string main_cpp = read_file_content(opts.target_directory / "src/main.cpp");
		// Verify C++17 override main.cpp content was selected (std::cout instead of std::println)
		assert(main_cpp.find("std::cout") != std::string::npos);
		assert(main_cpp.find("std::println") == std::string::npos);
	}

	// Cleanup
	std::filesystem::remove_all(temp_dir);

	std::cout << "project_template_manager verified successfully!" << std::endl;
	return 0;
}
