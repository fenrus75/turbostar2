#include "test_watchdog.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include "agentlib/tool_registry.h"
#include "project_manager.h"
#include "fs_utils.h"

using namespace agentlib;

void write_file(const std::filesystem::path &path, const std::string &content)
{
	std::filesystem::create_directories(path.parent_path());
	std::ofstream out(path, std::ios::binary);
	out << content;
}

std::string read_file(const std::filesystem::path &path)
{
	std::ifstream in(path, std::ios::binary);
	return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

int main()
{
	test_watchdog::setup_watchdog(30);

	std::filesystem::path temp_home = std::filesystem::absolute("./test_image_tools_home");
	if (std::filesystem::exists(temp_home)) {
		std::filesystem::remove_all(temp_home);
	}
	std::filesystem::create_directories(temp_home);
	setenv("HOME", temp_home.c_str(), 1);

	project_manager::get_instance().initialize();

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;
	ctx.fs_security.set_working_directory(project_manager::get_instance().get_project_root());
	ctx.fs_security.add_allowed_root(project_manager::get_instance().get_project_root(), access_type::read);
	ctx.fs_security.add_allowed_root(project_manager::get_instance().get_project_root(), access_type::write);
	ctx.properties.active_families.push_back("image");

	std::cout << "Testing image_import & image_export..." << std::endl;

	// 1. Create a dummy PNG file in the workspace
	std::filesystem::path proj_root = project_manager::get_instance().get_project_root();
	std::filesystem::path dummy_img = proj_root / "dummy.png";
	std::string dummy_data = "PNG_DUMMY_DATA_12345";
	write_file(dummy_img, dummy_data);

	// 2. Import the dummy image into the VFS
	{
		nlohmann::json args = {{"filename", "dummy.png"}, {"output", "vfs_logo.png"}};
		std::string result = registry.execute_tool("image_import", args.dump(), ctx);
		std::cout << "Import result: " << result << std::endl;
		assert(result.find("Successfully imported") != std::string::npos);
	}

	// 3. Export the dummy image from VFS back to a new workspace file
	std::filesystem::path exported_img = proj_root / "exported.png";
	{
		nlohmann::json args = {{"name", "vfs_logo.png"}, {"filename", "exported.png"}};
		std::string result = registry.execute_tool("image_export", args.dump(), ctx);
		std::cout << "Export result: " << result << std::endl;
		assert(result.find("Successfully exported") != std::string::npos);
	}

	// 4. Verify contents match
	assert(std::filesystem::exists(exported_img));
	assert(read_file(exported_img) == dummy_data);

	// 5. Test validation failures
	{
		// Missing output
		nlohmann::json args = {{"filename", "dummy.png"}};
		auto prep = registry.prepare_tool("image_import", args.dump(), ctx);
		assert(prep.tool == nullptr);
	}
	{
		// Non-existent file
		nlohmann::json args = {{"filename", "nonexistent.png"}, {"output", "test.png"}};
		auto prep = registry.prepare_tool("image_import", args.dump(), ctx);
		assert(prep.tool == nullptr);
	}

	std::cout << "image_import & image_export unit tests passed!" << std::endl;

	// Cleanup
	std::filesystem::remove_all(temp_home);
	std::filesystem::remove(dummy_img);
	std::filesystem::remove(exported_img);

	return 0;
}
