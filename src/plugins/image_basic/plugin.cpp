#include "agentlib/tool_registry.h"

extern "C" {

const char *plugin_name(void)
{
	return "Basic Image Operations";
}

const char *plugin_description(void)
{
	return "Provides basic image manipulation capabilities (e.g. image_resize).";
}

void register_image_resize(void);
void unregister_image_resize(void);
void register_image_crop(void);
void unregister_image_crop(void);
void register_image_rotate(void);
void unregister_image_rotate(void);
void register_image_mirror(void);
void unregister_image_mirror(void);

void plugin_run(void)
{
	register_image_resize();
	register_image_crop();
	register_image_rotate();
	register_image_mirror();
	agentlib::tool_registry::get_instance().register_tool_family("image", "Activate when performing image manipulation or editing");
}

void plugin_unload(void)
{
	unregister_image_resize();
	unregister_image_crop();
	unregister_image_rotate();
	unregister_image_mirror();
	agentlib::tool_registry::get_instance().unregister_tool_family("image");
}

}
