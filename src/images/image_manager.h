#pragma once

#include <string>
#include <vector>
#include <mutex>

namespace images
{

struct image_metadata {
	std::string sha256;
	std::vector<std::string> file_ids;
	std::vector<std::string> names;
	long long created_at = 0;
	std::string mime_type;
	int width = 0;
	int height = 0;
	size_t size_bytes = 0;
};

class image_manager
{
      public:
	static image_manager &get_instance();

	void initialize();

	// Resolves images:// URIs to absolute local paths in the cache.
	// Supports:
	// - images://by-sha256/<hash>
	// - images://by-file-id/<id>
	// - images://by-name/<alias>.png (or other extension)
	// - images://<name> (equivalent to images://by-name/<name>)
	std::string resolve_uri(const std::string &uri);

	// Generates a unique temporary image file path under cache_root
	std::string get_temp_image_path();

	// Ingests an image from temp_path, computes its hash, parses dimensions,
	// copies it to the cache directory, and associates it with the given name.
	// Returns the resolved images://by-sha256/ URI.
	std::string ingest_image(const std::string &temp_path, const std::string &alias = "");

	// Retrieves metadata for a resolved URI
	bool get_metadata(const std::string &uri, image_metadata &out_meta);

	// Registers a remote provider file_id mapping for a resolved VFS image
	bool register_file_id(const std::string &uri, const std::string &file_id);

	// Clears all cache files and resets mappings.json
	void clear_cache();

	// For test/introspection: get all mappings
	std::vector<image_metadata> get_all_mappings();

      private:
	image_manager() = default;
	~image_manager() = default;
	image_manager(const image_manager &) = delete;
	image_manager &operator=(const image_manager &) = delete;

	void load_mappings();
	void save_mappings();
	std::string get_cache_dir();
	std::string get_mappings_path();

	// Mutex protecting access to the in-memory mappings list and mappings.json file operations.
	// This lock must be acquired when reading or updating the mappings vector to ensure
	// thread-safe operations when accessed concurrently by multiple plugins or threads.
	mutable std::mutex mutex_;

	std::vector<image_metadata> mappings_;
};

} // namespace images
