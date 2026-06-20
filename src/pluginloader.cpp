#include "pluginloader.h"
#include <filesystem>
#include <dlfcn.h>

plugin_loader::plugin_loader()
{
}

plugin_loader::~plugin_loader()
{
	for (void *handle : loaded_handles_) {
		if (handle) {
			dlclose(handle);
		}
	}
}

void plugin_loader::load_all_plugins()
{
#ifdef PLUGIN_DIR
	std::filesystem::path plugin_path(PLUGIN_DIR);
	if (!std::filesystem::exists(plugin_path) || !std::filesystem::is_directory(plugin_path)) {
		return;
	}

	for (const auto &entry : std::filesystem::directory_iterator(plugin_path)) {
		if (entry.is_regular_file() && entry.path().extension() == ".so") {
			void *handle = dlopen(entry.path().c_str(), RTLD_LOCAL | RTLD_LAZY);
			if (handle) {
				union {
					void *ptr;
					void (*func)(void);
				} cast;

				cast.ptr = dlsym(handle, "plugin_run");
				if (cast.func) {
					cast.func();
				}
				loaded_handles_.push_back(handle);
			}
		}
	}
#endif
}

