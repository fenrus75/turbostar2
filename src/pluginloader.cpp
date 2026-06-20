#include "pluginloader.h"

plugin_loader::plugin_loader()
{
}

plugin_loader::~plugin_loader()
{
}

void plugin_loader::load_all_plugins()
{
	// For now, this is a dummy stub implementation.
	// It will load plugins from the directory specified by PLUGIN_DIR.
#ifdef PLUGIN_DIR
	// The path to load plugins from is defined at compile time.
	(void)PLUGIN_DIR;
#endif
}
