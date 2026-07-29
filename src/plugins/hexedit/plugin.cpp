#include "agentlib/tool_registry.h"

extern "C" {

const char *plugin_name(void)
{
	return "Hex Editor Tools";
}

const char *plugin_description(void)
{
	return "Provides structural hexdump inspection and raw byte patching tools (hexdump, hexwrite).";
}

void register_hexdump(void);
void unregister_hexdump(void);
void register_hexwrite(void);
void unregister_hexwrite(void);
void register_hexinspect(void);
void unregister_hexinspect(void);

void plugin_run(void)
{
	register_hexdump();
	register_hexwrite();
	register_hexinspect();
	agentlib::tool_registry::get_instance().register_tool_family(
		"hexedit",
		"Activate when viewing or writing raw hex data in binary/text files",
		R"(### Hexedit & Binary Manipulation Tool Family (`hexedit` / `binary`)

Tools in this family enable direct binary file inspection, structural parsing, range extraction, and byte-level editing without requiring external shell utilities.

#### Key Workflows & Capability Overview
- **Structure Discovery (`hexinspect`)**: Automatically parses MIME structures (TAR, ELF, PNG, JPEG, ZIP, etc.) and returns offset tables, headers, and metadata.
- **Raw Dump & Annotation (`hexdump`)**: Formats byte ranges with offset numbers, hex values, and ASCII representations.
- **Targeted Slice Extraction (`data_decompress`)**: Extract exact byte slices from archives or binary payloads using `format='none'`, combined with byte `offset` and `length`.
- **Byte Patching (`hexwrite`)**: Overwrite specific byte ranges at exact offsets.

---

#### Workflow Example: Inspecting & Extracting Files from Archives (.tar.gz)

When working with binary archives in sandbox environments, you can unpack and extract files natively without dropping into shell commands:

1. **Decompress the Archive**:
   `data_decompress(input_file="archive.tar.gz", output_file="tmp://archive.tar", format="gzip")`

2. **Inspect Internal Offsets**:
   `hexinspect(path="tmp://archive.tar", start_offset=0, size=512)`
   *Returns the archive layout table showing filenames, data offsets (e.g. `0x200` / `512`), and byte lengths (e.g. `11215`).*

3. **Extract Target File Slice**:
   `data_decompress(input_file="tmp://archive.tar", output_file="tmp://extracted_file.cpp", format="none", offset=512, length=11215)`

4. **Read or Process Extracted Artifact**:
   `fs_read_lines(path="tmp://extracted_file.cpp", start_line=1, end_line=20)`
)"
	);
}

void plugin_unload(void)
{
	unregister_hexdump();
	unregister_hexwrite();
	unregister_hexinspect();
	agentlib::tool_registry::get_instance().unregister_tool_family("hexedit");
}

}