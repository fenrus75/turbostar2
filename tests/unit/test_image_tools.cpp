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
#include "images/image_manager.h"
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
	project_manager::get_instance().initialize();
	test_watchdog::init_plugin_environment();

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;
	auto global_vfs = std::make_unique<agentlib::virtual_file_system>();
	ctx.fs_security.set_vfs(global_vfs.get());
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

	// 4b. Export dummy image from VFS to virtual tmp:// namespace
	{
		nlohmann::json args = {{"name", "vfs_logo.png"}, {"filename", "tmp://exported_logo.png"}};
		std::string result = registry.execute_tool("image_export", args.dump(), ctx);
		std::cout << "Export to VFS result: " << result << std::endl;
		assert(result.find("Successfully exported") != std::string::npos);
		assert(result.find("tmp://exported_logo.png") != std::string::npos);

		auto vfs = ctx.fs_security.get_vfs();
		assert(vfs != nullptr);
		auto read_opt = vfs->read_file("tmp://exported_logo.png");
		assert(read_opt.has_value());
		assert((*read_opt)->view().size() > 0);
	}

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
		// Rich success text: must report dimensions, byte size, and MIME for a real image.
		assert(result.find("x") != std::string::npos);
		assert(result.find("bytes") != std::string::npos);
		assert(result.find("image/jpeg") != std::string::npos);
		std::cout << "Import result (real image): " << result << std::endl;
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
		// Rich success text: must report original and target dimensions.
		assert(result.find("360x274") != std::string::npos);
		assert(result.find("100x100") != std::string::npos);

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
		// Rich success text: must report original and cropped dimensions plus region.
		assert(result.find("360x274") != std::string::npos);
		assert(result.find("50x50") != std::string::npos);
		assert(result.find("region x=10..59, y=10..59") != std::string::npos);

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
		// Rich success text: must report angle and resulting (swapped) dimensions.
		assert(result.find("90 degrees") != std::string::npos);
		assert(result.find("360x274") != std::string::npos);
		assert(result.find("274x360") != std::string::npos);

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
		// Rich success text: must echo the direction and confirm size is unchanged.
		assert(result.find("horizontal") != std::string::npos);
		assert(result.find("360x274") != std::string::npos);

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
		// Rich success text: grayscale reports average luminance (0-255) instead of white/black %.
		assert(result.find("average luminance") != std::string::npos);
		assert(result.find("/255") != std::string::npos);

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
		// Rich success text: threshold (binary) reports white/black pixel percentages.
		assert(result.find("% white") != std::string::npos);
		assert(result.find("% black") != std::string::npos);

		std::filesystem::path out_file = proj_root / "logo_threshold.jpg";
		nlohmann::json export_args = {{"name", "logo_threshold.jpg"}, {"filename", "logo_threshold.jpg"}};
		registry.execute_tool("image_export", export_args.dump(), ctx);
		assert(std::filesystem::exists(out_file));
		assert(std::filesystem::file_size(out_file) > 0);
		std::filesystem::remove(out_file);
	}

	// 12. Test image_compose
	{
		nlohmann::json args = {
			{"main_image", "logo.jpg"},
			{"small_image", "logo.jpg"},
			{"x", 10},
			{"y", 10},
			{"output", "logo_composed.jpg"}
		};
		std::string result = registry.execute_tool("image_compose", args.dump(), ctx);
		std::cout << "Compose result: " << result << std::endl;
		assert(result.find("Successfully composed") != std::string::npos);

		std::filesystem::path out_file = proj_root / "logo_composed.jpg";
		nlohmann::json export_args = {{"name", "logo_composed.jpg"}, {"filename", "logo_composed.jpg"}};
		registry.execute_tool("image_export", export_args.dump(), ctx);
		assert(std::filesystem::exists(out_file));
		assert(std::filesystem::file_size(out_file) > 0);
		std::filesystem::remove(out_file);
	}

	// 13. Test image_thumbnail output filter
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

	// 14. Test image_getdata tool
	{
		std::cout << "Testing image_getdata..." << std::endl;
		nlohmann::json getdata_args = {{"filename", "vfs_logo.png"}};
		std::string result = registry.execute_tool("image_getdata", getdata_args.dump(), ctx);
		std::cout << "image_getdata result: " << result.substr(0, 100) << "..." << std::endl;
		assert(!result.empty());
		assert(result.find("data:image/png;base64,") != std::string::npos);

		// Test size limit rejection on large image (logo.jpg is 84KB > 50KB limit)
		nlohmann::json large_args = {{"filename", "logo.jpg"}};
		std::string large_result = registry.execute_tool("image_getdata", large_args.dump(), ctx);
		std::cout << "Large image_getdata result: " << large_result << std::endl;
		assert(large_result.find("exceeds maximum size limit") != std::string::npos);

		// Test explicit custom max_bytes override (e.g. 100KB > 84KB)
		nlohmann::json custom_args = {{"filename", "logo.jpg"}, {"max_bytes", 100000}};
		std::string custom_result = registry.execute_tool("image_getdata", custom_args.dump(), ctx);
		assert(custom_result.find("data:image/jpeg;base64,") != std::string::npos);
	}

	// 15. Test Image Provenance & Origin Chain Tracking
	{
		std::cout << "Testing Image Provenance & Origin Chain Tracking..." << std::endl;

		// 15a. Rotate logo.jpg (alias) -> logo_chain_rot.jpg
		nlohmann::json rot_args = {{"name", "logo.jpg"}, {"degrees", 90}, {"output", "logo_chain_rot.jpg"}};
		std::string rot_res = registry.execute_tool("image_rotate", rot_args.dump(), ctx);
		assert(rot_res.find("Successfully rotated") != std::string::npos);

		// 15b. Crop logo_chain_rot.jpg (alias) -> logo_chain_crop.jpg
		nlohmann::json crop_args = {{"name", "logo_chain_rot.jpg"}, {"x", 5}, {"y", 5}, {"width", 20}, {"height", 20}, {"output", "logo_chain_crop.jpg"}};
		std::string crop_res = registry.execute_tool("image_crop", crop_args.dump(), ctx);
		assert(crop_res.find("Successfully cropped") != std::string::npos);

		// 15c. Verify metadata origin fields
		images::image_metadata meta;
		bool meta_ok = images::image_manager::get_instance().get_metadata("images://logo_chain_crop.jpg", meta);
		assert(meta_ok);
		std::cout << "Origin file hash: " << meta.origin_file << std::endl;
		std::cout << "Origin ops: " << meta.origin_ops << std::endl;
		assert(!meta.origin_file.empty());
		assert(meta.origin_ops == "crop(5,5,20,20)");

		// 15d. Verify formatted origin chain
		std::string chain_str = images::image_manager::get_instance().format_origin_chain("images://logo_chain_crop.jpg");
		std::cout << "Formatted origin chain: " << chain_str << std::endl;
		assert(chain_str.find("images://logo.jpg") != std::string::npos);
		assert(chain_str.find("rotate(90)") != std::string::npos);
		assert(chain_str.find("crop(5,5,20,20)") != std::string::npos);
		assert(chain_str.find("images://logo_chain_crop.jpg") != std::string::npos);
	}

	std::cout << "All basic image operation tests passed successfully!" << std::endl;

	// Cleanup
	std::filesystem::remove_all(temp_home);
	std::filesystem::remove(dummy_img);
	std::filesystem::remove(exported_img);
	std::filesystem::remove(test_logo);

	return 0;
}
