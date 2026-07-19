#include "agentlib/tool_registry.h"

extern "C" {

const char *plugin_name(void)
{
	return "Binary Tools";
}

const char *plugin_description(void)
{
	return "Provides tools for compressing and decompressing binary data.";
}

void register_data_compress(void);
void unregister_data_compress(void);
void register_data_decompress(void);
void unregister_data_decompress(void);

void plugin_run(void)
{
	register_data_compress();
	register_data_decompress();
	agentlib::tool_registry::get_instance().register_tool_family(
		"binary",
		"Activate when you need to inspect or manipulate compressed binary data",
		"The 'binary' tool family provides tools for compressing and decompressing data streams.\n\n"
		"Key Tools:\n"
		"- data_decompress: Extracts and decompresses data from various sources (files, data URLs, hex/base64 strings). It allows reading embedded streams (like PDF objects) using the offset and length parameters.\n"
		"- data_compress: Compresses data into various formats (zstd, gzip, zlib, etc.) and returns it as text, hex, base64, or writes to a file."
	);
}

void plugin_unload(void)
{
	unregister_data_compress();
	unregister_data_decompress();
	agentlib::tool_registry::get_instance().unregister_tool_family("binary");
}

}
