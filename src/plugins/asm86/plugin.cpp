extern "C" {

const char *plugin_name(void)
{
	return "x86 Assembler & Disassembler";
}

const char *plugin_description(void)
{
	return "Provides tools for assembling and disassembling x86 machine instructions.";
}

void register_x86_assemble(void);
void unregister_x86_assemble(void);
void register_x86_disassemble(void);
void unregister_x86_disassemble(void);

/*
 * Every plugin must implement a plugin_run function.
 * This serves as the entry point when the plugin is loaded dynamically.
 * extern "C" is used to prevent C++ name mangling so the host can find the symbol.
 */
void plugin_run(void)
{
	register_x86_assemble();
	register_x86_disassemble();
}

/*
 * plugin_unload is called programmatically before the plugin is closed with dlclose.
 */
void plugin_unload(void)
{
	unregister_x86_assemble();
	unregister_x86_disassemble();
}

}

