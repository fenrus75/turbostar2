// Tested source file: src/agentlib/tool_registry.cpp
#include "test_watchdog.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include "../../src/agentlib/tool_registry.h"
#include "../../src/agentlib/subagent_manager.h"
#include "../../src/agentlib/virtual_file_system.h"
#include "../../src/images/image_manager.h"
#include "../../src/project_manager.h"
#include "../../src/pluginloader.h"

using namespace agentlib;

int main()
{
	test_watchdog::setup_watchdog(30);

	project_manager::get_instance().initialize();
	subagent_manager::get_instance().initialize();
	images::image_manager::get_instance().initialize();

	// Create dummy test image under project root for alias resolution and import validation
	std::string dummy_img = (std::filesystem::path(project_manager::get_instance().get_project_root()) / "assets" / "logo.jpg").string();
	std::filesystem::create_directories(std::filesystem::path(dummy_img).parent_path());
	std::ofstream ofs(dummy_img, std::ios::binary);
	static const unsigned char png_1x1[] = {
		0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
		0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
		0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
		0x0a, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x00, 0x01, 0x00, 0x00,
		0x05, 0x00, 0x01, 0x0d, 0x0a, 0x2d, 0xb4, 0x00, 0x00, 0x00, 0x00, 0x49,
		0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82
	};
	ofs.write(reinterpret_cast<const char*>(png_1x1), sizeof(png_1x1));
	ofs.close();

	images::image_manager::get_instance().ingest_image(dummy_img, "canvas");
	images::image_manager::get_instance().ingest_image(dummy_img, "logo");
	images::image_manager::get_instance().ingest_image(dummy_img, "logo_gray");
	images::image_manager::get_instance().ingest_image(dummy_img, "logo_resized");
	images::image_manager::get_instance().ingest_image(dummy_img, "logo_thumb");
	images::image_manager::get_instance().ingest_image(dummy_img, "logo_crop");
	images::image_manager::get_instance().ingest_image(dummy_img, "bg");
	images::image_manager::get_instance().ingest_image(dummy_img, "overlay");

	plugin_loader::get_instance().load_all_plugins();

	virtual_file_system vfs;

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;
	ctx.fs_security.set_vfs(&vfs);
	ctx.fs_security.set_working_directory(project_manager::get_instance().get_project_root());
	ctx.fs_security.add_allowed_root(project_manager::get_instance().get_project_root(), access_type::read);
	ctx.fs_security.add_allowed_root(project_manager::get_instance().get_project_root(), access_type::write);
	ctx.fs_security.add_allowed_root(test_watchdog::get_global_test_home()->get_path(), access_type::read);
	ctx.fs_security.add_allowed_root(test_watchdog::get_global_test_home()->get_path(), access_type::write);

	auto validators = registry.get_all_registered_validators();
	std::cout << "Validating tool examples across " << validators.size() << " registered tools..." << std::endl;

	int total_examples = 0;
	int validated_examples = 0;

	for (const auto &validator : validators) {
		if (!validator) continue;
		std::string tool_name = validator->get_name();
		auto examples = validator->get_examples();
		for (size_t i = 0; i < examples.size(); ++i) {
			const auto &example = examples[i];
			total_examples++;
			std::string error;
			bool valid = validator->validate_args(example.input_args, ctx, error);
			if (!valid) {
				std::cerr << "FAIL: Example " << i << " ('" << example.title << "') for tool '" << tool_name
					  << "' failed validation: " << error << std::endl;
				std::cerr << "Input args: " << example.input_args.dump(2) << std::endl;
				assert(valid);
			}
			validated_examples++;
			std::cout << "  [OK] " << tool_name << " -> " << example.title << std::endl;
		}
	}

	std::cout << "Successfully validated " << validated_examples << " / " << total_examples << " tool examples!" << std::endl;
	plugin_loader::get_instance().unload_all_plugins();
	return 0;
}
