#pragma once

#include <vector>
#include <string>

/*
 * The plugin_loader class is responsible for locating, loading,
 * and running dynamic plugins (.so files) from the configured plugin directory.
 * Implements the singleton pattern.
 */
class plugin_loader {
public:
	struct plugin_info {
		std::string filename;
		std::string name;
		std::string description;
	};

	static plugin_loader& get_instance();

	/*
	 * Loads all plugins found in the PLUGIN_DIR directory.
	 */
	void load_all_plugins();

	/*
	 * Calls plugin_unload on all loaded plugins to unregister resources.
	 * This should be called early during the application shutdown sequence.
	 */
	void unload_all_plugins();

	/*
	 * Returns the list of metadata for loaded plugins.
	 */
	const std::vector<plugin_info>& get_plugins() const;

private:
	plugin_loader();
	~plugin_loader();

	// Disable copy/move constructors and assignment operators
	plugin_loader(const plugin_loader&) = delete;
	plugin_loader& operator=(const plugin_loader&) = delete;
	plugin_loader(plugin_loader&&) = delete;
	plugin_loader& operator=(plugin_loader&&) = delete;

	struct loaded_plugin {
		void *handle;
		plugin_info info;
	};

	std::vector<loaded_plugin> loaded_plugins_;
	std::vector<plugin_info> plugin_infos_;
	bool unloaded_ = false;
};


