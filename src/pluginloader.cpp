#include "pluginloader.h"
#include <filesystem>
#include <dlfcn.h>
#include <cstdlib>

plugin_loader& plugin_loader::get_instance()
{
	static plugin_loader instance;
	return instance;
}

plugin_loader::plugin_loader()
{
}

plugin_loader::~plugin_loader()
{
	unload_all_plugins();

	for (const auto &p : loaded_plugins_) {
		if (p.handle) {
			dlclose(p.handle);
		}
	}
}

void plugin_loader::unload_all_plugins()
{
	if (unloaded_) {
		return;
	}
	unloaded_ = true;

	for (const auto &p : loaded_plugins_) {
		if (p.handle) {
			union {
				void *ptr;
				void (*func)(void);
			} unload_cast;
			unload_cast.ptr = dlsym(p.handle, "plugin_unload");
			if (unload_cast.func) {
				unload_cast.func();
			}
		}
	}
}

const std::vector<plugin_loader::plugin_info>& plugin_loader::get_plugins() const
{
	return plugin_infos_;
}

void plugin_loader::load_all_plugins()
{
	std::filesystem::path plugin_path;
	const char *env_plugin_dir = std::getenv("TURBOSTAR_PLUGIN_DIR");
	if (env_plugin_dir && *env_plugin_dir) {
		plugin_path = env_plugin_dir;
	} else {
#ifdef PLUGIN_DIR
		plugin_path = PLUGIN_DIR;
#endif
	}

	if (plugin_path.empty() || !std::filesystem::exists(plugin_path) || !std::filesystem::is_directory(plugin_path)) {
		return;
	}

	for (const auto &entry : std::filesystem::directory_iterator(plugin_path)) {
		if (entry.is_regular_file() && entry.path().extension() == ".so") {
			void *handle = dlopen(entry.path().c_str(), RTLD_LOCAL | RTLD_LAZY);
			if (!handle) {
				fprintf(stderr, "dlopen failed for %s: %s\n", entry.path().c_str(), dlerror());
			} else {
				// Query metadata functions
				union {
					void *ptr;
					const char *(*func)(void);
				} name_cast, desc_cast;

				name_cast.ptr = dlsym(handle, "plugin_name");
				desc_cast.ptr = dlsym(handle, "plugin_description");

				std::string name;
				if (name_cast.func) {
					const char *res = name_cast.func();
					if (res) {
						name = res;
					}
				}
				if (name.empty()) {
					name = entry.path().stem().string();
				}

				std::string description;
				if (desc_cast.func) {
					const char *res = desc_cast.func();
					if (res) {
						description = res;
					}
				}

				union {
					void *ptr;
					void (*func)(void);
				} run_cast;

				run_cast.ptr = dlsym(handle, "plugin_run");
				if (run_cast.func) {
					run_cast.func();
				}

				plugin_info info;
				info.filename = entry.path().filename().string();
				info.name = name;
				info.description = description;

				loaded_plugin lp;
				lp.handle = handle;
				lp.info = info;

				loaded_plugins_.push_back(lp);
				plugin_infos_.push_back(info);
			}
		}
	}
}


