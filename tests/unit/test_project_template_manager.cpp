// Tested source file: src/project_template_manager.cpp
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "project_template_manager.h"
#include "test_watchdog.h"

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
	assert(templates.size() >= 7);

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

	// 4. Instantiate CMake C++ project (verifying CMAKE_EXPORT_COMPILE_COMMANDS)
	{
		turbostar::project_create_options opts;
		opts.project_name = "demo_cmake_cpp";
		opts.executable_name = "demo_cmake_cpp";
		opts.language = "C++";
		opts.buildsystem = "CMake";
		opts.language_standard = "C++23";
		opts.target_directory = temp_dir / "demo_cmake_cpp";
		opts.init_git = true;

		std::string err;
		bool ok = mgr.create_project(opts, err);
		assert(ok);
		assert(err.empty());

		assert(std::filesystem::exists(opts.target_directory / "CMakeLists.txt"));
		assert(std::filesystem::exists(opts.target_directory / "src/main.cpp"));
		assert(std::filesystem::exists(opts.target_directory / "AGENTS.md"));
		assert(std::filesystem::exists(opts.target_directory / ".gitignore"));

		std::string cmake_content = read_file_content(opts.target_directory / "CMakeLists.txt");
		assert(cmake_content.find("set(CMAKE_EXPORT_COMPILE_COMMANDS ON)") != std::string::npos);
		assert(cmake_content.find("project(demo_cmake_cpp") != std::string::npos);
		assert(cmake_content.find("set(CMAKE_CXX_STANDARD 23)") != std::string::npos);
	}

	// 5. Instantiate CMake C project (verifying CMAKE_EXPORT_COMPILE_COMMANDS)
	{
		turbostar::project_create_options opts;
		opts.project_name = "demo_cmake_c";
		opts.executable_name = "demo_cmake_c";
		opts.language = "C";
		opts.buildsystem = "CMake";
		opts.language_standard = "C17";
		opts.target_directory = temp_dir / "demo_cmake_c";
		opts.init_git = false;

		std::string err;
		bool ok = mgr.create_project(opts, err);
		assert(ok);
		assert(err.empty());

		assert(std::filesystem::exists(opts.target_directory / "CMakeLists.txt"));
		assert(std::filesystem::exists(opts.target_directory / "src/main.c"));

		std::string cmake_content = read_file_content(opts.target_directory / "CMakeLists.txt");
		assert(cmake_content.find("set(CMAKE_EXPORT_COMPILE_COMMANDS ON)") != std::string::npos);
		assert(cmake_content.find("project(demo_cmake_c") != std::string::npos);
		assert(cmake_content.find("set(CMAKE_C_STANDARD 17)") != std::string::npos);
	}

	// 5. Instantiate SystemVerilog Verilator project
	{
		turbostar::project_create_options opts;
		opts.project_name = "sv_demo";
		opts.executable_name = "sv_demo";
		opts.language = "SystemVerilog";
		opts.buildsystem = "Meson (Verilator)";
		opts.language_standard = "IEEE 1800-2017";
		opts.target_directory = temp_dir / "demo_sv";
		opts.init_git = true;

		std::string err;
		bool ok = mgr.create_project(opts, err);
		assert(ok);
		assert(err.empty());

		assert(std::filesystem::exists(opts.target_directory / "meson.build"));
		assert(std::filesystem::exists(opts.target_directory / "src/top.sv"));
		assert(std::filesystem::exists(opts.target_directory / "src/top_tb.sv"));
		assert(std::filesystem::exists(opts.target_directory / "AGENTS.md"));

		std::string meson_build = read_file_content(opts.target_directory / "meson.build");
		assert(meson_build.find("find_program('verilator'") != std::string::npos);
		assert(meson_build.find("sv_demo") != std::string::npos);

		std::string top_sv = read_file_content(opts.target_directory / "src/top.sv");
		assert(top_sv.find("module top") != std::string::npos);
		assert(top_sv.find("IEEE 1800-2017") != std::string::npos);
	}

	// 6. Instantiate SystemVerilog FPGA iCE40 project
	{
		turbostar::project_create_options opts;
		opts.project_name = "fpga_demo";
		opts.executable_name = "fpga_demo";
		opts.language = "SystemVerilog";
		opts.buildsystem = "Meson (FPGA)";
		opts.language_standard = "IEEE 1800-2017";
		opts.target_directory = temp_dir / "demo_fpga";
		opts.init_git = true;

		std::string err;
		bool ok = mgr.create_project(opts, err);
		assert(ok);
		assert(err.empty());

		assert(std::filesystem::exists(opts.target_directory / "meson.build"));
		assert(std::filesystem::exists(opts.target_directory / "pins.pcf"));
		assert(std::filesystem::exists(opts.target_directory / "8segbits.txt"));
		assert(std::filesystem::exists(opts.target_directory / "src/top.sv"));
		assert(std::filesystem::exists(opts.target_directory / "src/top_tb.sv"));
		assert(std::filesystem::exists(opts.target_directory / "AGENTS.md"));
		assert(std::filesystem::exists(opts.target_directory / ".gitignore"));

		std::string meson_build = read_file_content(opts.target_directory / "meson.build");
		assert(meson_build.find("find_program('yosys'") != std::string::npos);
		assert(meson_build.find("find_program('nextpnr-ice40'") != std::string::npos);
		assert(meson_build.find("find_program('icepack'") != std::string::npos);
		assert(meson_build.find("pins.pcf") != std::string::npos);
		assert(meson_build.find("fpga_demo") != std::string::npos);

		std::string pcf_content = read_file_content(opts.target_directory / "pins.pcf");
		assert(pcf_content.find("set_io clk P7") != std::string::npos);
		assert(pcf_content.find("set_io rst_n P8") != std::string::npos);
		assert(pcf_content.find("set_io led[0] N14") != std::string::npos);

		std::string top_sv = read_file_content(opts.target_directory / "src/top.sv");
		assert(top_sv.find("module top") != std::string::npos);
		assert(top_sv.find("counter_reg") != std::string::npos);

		std::string agents_md = read_file_content(opts.target_directory / "AGENTS.md");
		assert(agents_md.find("Lattice iCE40-HX8K") != std::string::npos);
		assert(agents_md.find("fpga_demo") != std::string::npos);
	}

	// Cleanup
	std::filesystem::remove_all(temp_dir);

	std::cout << "project_template_manager verified successfully!" << std::endl;
	return 0;
}
