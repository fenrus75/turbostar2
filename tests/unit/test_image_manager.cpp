#include "test_watchdog.h"
#include "../../src/images/image_manager.h"
#include "../../src/fs_utils.h"
#include "../../src/project_manager.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <cstring>

using namespace images;

int main()
{
	test_watchdog::setup_watchdog(30);

	// Initialize managers
	project_manager::get_instance().initialize();

	auto &manager = image_manager::get_instance();
	manager.initialize();

	std::cout << "Cleaning cache..." << std::endl;
	manager.clear_cache();

	assert(manager.get_all_mappings().empty());

	std::cout << "Creating mock PNG file..." << std::endl;
	std::string temp_png = manager.get_temp_image_path();
	{
		const unsigned char png_data[] = {
			0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, // Signature
			0x00, 0x00, 0x00, 0x0d,                         // IHDR Length
			'I', 'H', 'D', 'R',                             // IHDR Type
			0x00, 0x00, 0x00, 0x0a,                         // Width (10)
			0x00, 0x00, 0x00, 0x05,                         // Height (5)
			0x08, 0x02, 0x00, 0x00, 0x00,                   // Extra
			0x00, 0x00, 0x00, 0x00                          // CRC
		};
		std::ofstream ofs(temp_png, std::ios::binary);
		ofs.write(reinterpret_cast<const char *>(png_data), sizeof(png_data));
	}

	std::cout << "Ingesting PNG..." << std::endl;
	std::string uri_png = manager.ingest_image(temp_png, "screenshot.png");
	std::cout << "Ingested PNG URI: " << uri_png << std::endl;
	assert(!uri_png.empty());
	assert(uri_png.starts_with("images://by-sha256/"));

	// Verify temp file got cleaned up
	assert(!std::filesystem::exists(temp_png));

	std::cout << "Checking PNG metadata..." << std::endl;
	image_metadata meta_png;
	bool success = manager.get_metadata(uri_png, meta_png);
	assert(success);
	assert(meta_png.mime_type == "image/png");
	assert(meta_png.width == 10);
	assert(meta_png.height == 5);
	assert(std::find(meta_png.names.begin(), meta_png.names.end(), "screenshot.png") != meta_png.names.end());

	std::cout << "Creating mock JPEG file..." << std::endl;
	std::string temp_jpg = manager.get_temp_image_path();
	{
		const unsigned char jpeg_data[] = {
			0xff, 0xd8,                                     // SOI
			0xff, 0xe0,                                     // APP0 Marker (to test skipping markers)
			0x00, 0x05,                                     // Length 5
			0x01, 0x02, 0x03,                               // App data
			0xff, 0xc0,                                     // SOF0 Marker
			0x00, 0x0b,                                     // Length 11
			0x08,                                           // Precision
			0x00, 0x0f,                                     // Height (15)
			0x00, 0x14,                                     // Width (20)
			0x03, 0x01, 0x22, 0x00, 0x02,                   // Components
			0xff, 0xd9                                      // EOI
		};
		std::ofstream ofs(temp_jpg, std::ios::binary);
		ofs.write(reinterpret_cast<const char *>(jpeg_data), sizeof(jpeg_data));
	}

	std::cout << "Ingesting JPEG..." << std::endl;
	std::string uri_jpg = manager.ingest_image(temp_jpg, "photo.jpg");
	std::cout << "Ingested JPEG URI: " << uri_jpg << std::endl;
	assert(!uri_jpg.empty());
	assert(uri_jpg.starts_with("images://by-sha256/"));

	std::cout << "Checking JPEG metadata..." << std::endl;
	image_metadata meta_jpg;
	success = manager.get_metadata(uri_jpg, meta_jpg);
	assert(success);
	assert(meta_jpg.mime_type == "image/jpeg");
	assert(meta_jpg.width == 20);
	assert(meta_jpg.height == 15);

	std::cout << "Resolving URIs..." << std::endl;
	// Resolve by sha256
	std::string path_sha256 = manager.resolve_uri(uri_png);
	assert(!path_sha256.empty());
	assert(std::filesystem::exists(path_sha256));

	// Resolve by name
	std::string path_name = manager.resolve_uri("images://by-name/screenshot.png");
	assert(path_name == path_sha256);

	// Resolve by short name alias
	std::string path_short = manager.resolve_uri("images://screenshot.png");
	assert(path_short == path_sha256);

	std::cout << "Registering file ID..." << std::endl;
	success = manager.register_file_id(uri_png, "file-openai-xyz123");
	assert(success);

	std::string path_fid = manager.resolve_uri("images://by-file-id/file-openai-xyz123");
	assert(path_fid == path_sha256);

	// Test metadata persistence by reinitializing
	std::cout << "Reinitializing manager to test mappings.json loading..." << std::endl;
	manager.initialize();

	image_metadata reloaded_meta;
	success = manager.get_metadata(uri_png, reloaded_meta);
	assert(success);
	assert(reloaded_meta.mime_type == "image/png");
	assert(reloaded_meta.width == 10);
	assert(reloaded_meta.height == 5);
	assert(std::find(reloaded_meta.file_ids.begin(), reloaded_meta.file_ids.end(), "file-openai-xyz123") != reloaded_meta.file_ids.end());

	std::cout << "Testing cache clearing..." << std::endl;
	manager.clear_cache();
	assert(manager.get_all_mappings().empty());
	assert(manager.resolve_uri(uri_png).empty());

	std::cout << "Testing assistant generated image base64 interception..." << std::endl;
	{
		const unsigned char png_data[] = {
			0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, // Signature
			0x00, 0x00, 0x00, 0x0d,                         // IHDR Length
			'I', 'H', 'D', 'R',                             // IHDR Type
			0x00, 0x00, 0x00, 0x05,                         // Width (5)
			0x00, 0x00, 0x00, 0x05,                         // Height (5)
			0x08, 0x02, 0x00, 0x00, 0x00,                   // Extra
			0x00, 0x00, 0x00, 0x00                          // CRC
		};
		std::string b64 = fs_utils::base64_encode(std::span<const unsigned char>(png_data, sizeof(png_data)));
		std::string content = "Here is your generated image: data:image/png;base64," + b64 + " Enjoy!";
		
		std::string updated_content;
		size_t last_pos = 0;
		while (true) {
			size_t start = content.find("data:image/", last_pos);
			if (start == std::string::npos) {
				updated_content += content.substr(last_pos);
				break;
			}
			
			updated_content += content.substr(last_pos, start - last_pos);
			
			size_t base64_start = content.find(";base64,", start);
			if (base64_start == std::string::npos) {
				updated_content += "data:image/";
				last_pos = start + 11;
				continue;
			}
			
			size_t data_start = base64_start + 8;
			size_t data_end = data_start;
			while (data_end < content.length() && 
			       (std::isalnum(content[data_end]) || 
			        content[data_end] == '+' || 
			        content[data_end] == '/' || 
			        content[data_end] == '=')) {
				data_end++;
			}
			
			std::string b64_data = content.substr(data_start, data_end - data_start);
			
			std::vector<unsigned char> decoded = fs_utils::base64_decode(b64_data);
			assert(!decoded.empty());
			assert(decoded.size() == sizeof(png_data));
			assert(std::memcmp(decoded.data(), png_data, sizeof(png_data)) == 0);
			
			std::string temp_path = manager.get_temp_image_path();
			std::ofstream ofs(temp_path, std::ios::binary);
			assert(ofs);
			ofs.write(reinterpret_cast<const char*>(decoded.data()), decoded.size());
			ofs.close();
			
			std::string vfs_uri = manager.ingest_image(temp_path);
			assert(!vfs_uri.empty());
			assert(vfs_uri.starts_with("images://by-sha256/"));
			
			updated_content += vfs_uri;
			last_pos = data_end;
		}
		
		std::cout << "Intercepted updated content: " << updated_content << std::endl;
		assert(updated_content.find("images://by-sha256/") != std::string::npos);
		assert(updated_content.find("data:image/") == std::string::npos);
	}

	std::cout << "\nAll image_manager tests verified successfully!" << std::endl;
	return 0;
}
