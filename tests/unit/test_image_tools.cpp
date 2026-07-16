#include "test_watchdog.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include "agentlib/tool_registry.h"
#include "agentlib/skill_manager.h"
#include "agentlib/command_registry.h"
#include "filter_registry.h"
#include "pluginloader.h"
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

	// Force initialization of singletons
	(void)tool_registry::get_instance();
	(void)command_registry::get_instance();
	(void)skill_manager::get_instance();
	(void)filter_registry::get_instance();

	project_manager::get_instance().initialize();

	// Load dynamic plugins (registers image_basic tools & filter)
	auto &loader = plugin_loader::get_instance();
	loader.load_all_plugins();

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;
	ctx.fs_security.set_working_directory(project_manager::get_instance().get_project_root());
	ctx.fs_security.add_allowed_root(project_manager::get_instance().get_project_root(), access_type::read);
	ctx.fs_security.add_allowed_root(project_manager::get_instance().get_project_root(), access_type::write);
	ctx.properties.active_families.push_back("image");

	std::filesystem::path proj_root = project_manager::get_instance().get_project_root();

	std::cout << "Testing image_import & image_export..." << std::endl;

	// 1. Create a dummy PNG file in the workspace
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

	// 5. Test validation failures on import
	{
		nlohmann::json args = {{"filename", "dummy.png"}};
		auto prep = registry.prepare_tool("image_import", args.dump(), ctx);
		assert(prep.tool == nullptr);
	}

	std::cout << "Testing Basic Image Operations on logo.jpg..." << std::endl;

	// Load real test image (tests/data/logo.jpg)
	std::filesystem::path real_src = proj_root / "tests/data/logo.jpg";
	assert(std::filesystem::exists(real_src));

	// Copy to a workspace temp name
	std::filesystem::path test_logo = proj_root / "test_logo.jpg";
	std::filesystem::copy_file(real_src, test_logo, std::filesystem::copy_options::overwrite_existing);

	// Import test_logo.jpg into VFS
	{
		nlohmann::json args = {{"filename", "test_logo.jpg"}, {"output", "logo.jpg"}};
		std::string result = registry.execute_tool("image_import", args.dump(), ctx);
		assert(result.find("Successfully imported") != std::string::npos);
	}

	// Export logo.jpg as a PNG file and verify format conversion
	std::filesystem::path converted_img = proj_root / "converted.png";
	if (std::filesystem::exists(converted_img)) {
		std::filesystem::remove(converted_img);
	}
	{
		nlohmann::json args = {{"name", "logo.jpg"}, {"filename", "converted.png"}};
		std::string result = registry.execute_tool("image_export", args.dump(), ctx);
		assert(result.find("Successfully exported") != std::string::npos);
	}
	assert(std::filesystem::exists(converted_img));
	{
		std::string img_header = read_file(converted_img).substr(0, 4);
		// If GraphicsMagick is compiled in, it should have converted the file format to PNG
#ifdef HAS_GRAPHICSMAGICK
		assert(img_header == "\x89PNG");
#else
		// If not compiled with GraphicsMagick, it falls back to raw copy, so it remains JPEG format
		assert(img_header[0] == '\xff' && img_header[1] == '\xd8' && img_header[2] == '\xff');
#endif
	}
	if (std::filesystem::exists(converted_img)) {
		std::filesystem::remove(converted_img);
	}

	// 6. Test image_resize
	{
		nlohmann::json args = {{"name", "logo.jpg"}, {"newX", 100}, {"newY", 100}, {"output", "logo_resized.jpg"}};
		std::string result = registry.execute_tool("image_resize", args.dump(), ctx);
		std::cout << "Resize result: " << result << std::endl;
		assert(result.find("Successfully resized") != std::string::npos);

		// Export and check existence
		std::filesystem::path out_file = proj_root / "logo_resized.jpg";
		nlohmann::json export_args = {{"name", "logo_resized.jpg"}, {"filename", "logo_resized.jpg"}};
		std::string export_res = registry.execute_tool("image_export", export_args.dump(), ctx);
		assert(std::filesystem::exists(out_file));
		assert(std::filesystem::file_size(out_file) > 0);
		std::filesystem::remove(out_file);
	}

	// 7. Test image_crop
	{
		nlohmann::json args = {{"name", "logo.jpg"}, {"x", 10}, {"y", 10}, {"width", 50}, {"height", 50}, {"output", "logo_cropped.jpg"}};
		std::string result = registry.execute_tool("image_crop", args.dump(), ctx);
		std::cout << "Crop result: " << result << std::endl;
		assert(result.find("Successfully cropped") != std::string::npos);

		std::filesystem::path out_file = proj_root / "logo_cropped.jpg";
		nlohmann::json export_args = {{"name", "logo_cropped.jpg"}, {"filename", "logo_cropped.jpg"}};
		registry.execute_tool("image_export", export_args.dump(), ctx);
		assert(std::filesystem::exists(out_file));
		assert(std::filesystem::file_size(out_file) > 0);
		std::filesystem::remove(out_file);
	}

	// 8. Test image_rotate
	{
		nlohmann::json args = {{"name", "logo.jpg"}, {"degrees", 90}, {"output", "logo_rotated.jpg"}};
		std::string result = registry.execute_tool("image_rotate", args.dump(), ctx);
		std::cout << "Rotate result: " << result << std::endl;
		assert(result.find("Successfully rotated") != std::string::npos);

		std::filesystem::path out_file = proj_root / "logo_rotated.jpg";
		nlohmann::json export_args = {{"name", "logo_rotated.jpg"}, {"filename", "logo_rotated.jpg"}};
		registry.execute_tool("image_export", export_args.dump(), ctx);
		assert(std::filesystem::exists(out_file));
		assert(std::filesystem::file_size(out_file) > 0);
		std::filesystem::remove(out_file);
	}

	// 9. Test image_mirror
	{
		nlohmann::json args = {{"name", "logo.jpg"}, {"direction", "horizontal"}, {"output", "logo_mirrored.jpg"}};
		std::string result = registry.execute_tool("image_mirror", args.dump(), ctx);
		std::cout << "Mirror result: " << result << std::endl;
		assert(result.find("Successfully mirrored") != std::string::npos);

		std::filesystem::path out_file = proj_root / "logo_mirrored.jpg";
		nlohmann::json export_args = {{"name", "logo_mirrored.jpg"}, {"filename", "logo_mirrored.jpg"}};
		registry.execute_tool("image_export", export_args.dump(), ctx);
		assert(std::filesystem::exists(out_file));
		assert(std::filesystem::file_size(out_file) > 0);
		std::filesystem::remove(out_file);
	}

	// 10. Test image_grayscale
	{
		nlohmann::json args = {{"name", "logo.jpg"}, {"output", "logo_grayscale.jpg"}};
		std::string result = registry.execute_tool("image_grayscale", args.dump(), ctx);
		std::cout << "Grayscale result: " << result << std::endl;
		assert(result.find("Successfully converted") != std::string::npos);

		std::filesystem::path out_file = proj_root / "logo_grayscale.jpg";
		nlohmann::json export_args = {{"name", "logo_grayscale.jpg"}, {"filename", "logo_grayscale.jpg"}};
		registry.execute_tool("image_export", export_args.dump(), ctx);
		assert(std::filesystem::exists(out_file));
		assert(std::filesystem::file_size(out_file) > 0);
		std::filesystem::remove(out_file);
	}

	// 11. Test image_threshold
	{
		nlohmann::json args = {{"name", "logo.jpg"}, {"level", 0.5}, {"output", "logo_threshold.jpg"}};
		std::string result = registry.execute_tool("image_threshold", args.dump(), ctx);
		std::cout << "Threshold result: " << result << std::endl;
		assert(result.find("Successfully applied") != std::string::npos);

		std::filesystem::path out_file = proj_root / "logo_threshold.jpg";
		nlohmann::json export_args = {{"name", "logo_threshold.jpg"}, {"filename", "logo_threshold.jpg"}};
		registry.execute_tool("image_export", export_args.dump(), ctx);
		assert(std::filesystem::exists(out_file));
		assert(std::filesystem::file_size(out_file) > 0);
		std::filesystem::remove(out_file);
	}

	// 12. Test image_thumbnail output filter
	{
		assert(filter_registry::get_instance().has_filter("image_thumbnail"));

		nlohmann::json filter_args = {{"path", test_logo.string()}, {"width", 10}, {"height", 10}};
		bool filter_success = false;
		std::string filter_res = filter_registry::get_instance().apply_filter("image_thumbnail", filter_args.dump(), filter_success);
		assert(filter_success);
		std::cout << "Thumbnail filter result length: " << filter_res.length() << std::endl;

		nlohmann::json parsed_res = nlohmann::json::parse(filter_res);
		assert(parsed_res.contains("width"));
		assert(parsed_res.contains("height"));
		assert(parsed_res.contains("cells"));
		assert(parsed_res["width"] == 10);
		assert(parsed_res["height"] == 10);
		assert(parsed_res["cells"].is_array());
		assert(parsed_res["cells"].size() == 100);
	}

	std::cout << "All basic image operation tests passed successfully!" << std::endl;

	// Cleanup
	std::filesystem::remove_all(temp_home);
	std::filesystem::remove(dummy_img);
	std::filesystem::remove(exported_img);
	std::filesystem::remove(test_logo);

	return 0;
}
