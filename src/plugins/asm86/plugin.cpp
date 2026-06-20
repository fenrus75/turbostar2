extern "C" {

/*
 * Every plugin must implement a plugin_run function.
 * This serves as the entry point when the plugin is loaded dynamically.
 * extern "C" is used to prevent C++ name mangling so the host can find the symbol.
 */
void plugin_run(void)
{
	// asm86 plugin entry point stub
}

}
