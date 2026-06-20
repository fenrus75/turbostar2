#pragma once

/*
 * The plugin_loader class is responsible for locating, loading,
 * and running dynamic plugins (.so files) from the configured plugin directory.
 */
class plugin_loader {
public:
	plugin_loader();
	~plugin_loader();

	/*
	 * Loads all plugins found in the PLUGIN_DIR directory.
	 */
	void load_all_plugins();
};
