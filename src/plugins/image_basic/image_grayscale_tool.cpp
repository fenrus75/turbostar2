#include "plugins/image_basic/image_grayscale_tool.h"
#include <Magick++.h>
#include "images/image_manager.h"
#include <exception>

namespace tools
{

image_grayscale_tool::image_grayscale_tool(image_grayscale_args args)
    : llm_tool_action("Converting image to grayscale"), args_(std::move(args))
{
}

bool image_grayscale_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string image_grayscale_tool::execute(agentlib::tool_context &ctx)
{
	try {
		Magick::InitializeMagick(nullptr);

		Magick::Image img(args_.safe_path);
		img.type(Magick::GrayscaleType);

		std::string temp_out = images::image_manager::get_instance().get_temp_image_path();
		img.write(temp_out);

		std::string target_alias = args_.name;
		if (args_.output.has_value() && !args_.output->empty()) {
			target_alias = *args_.output;
		}

		std::string new_uri = images::image_manager::get_instance().ingest_image(temp_out, target_alias);
		if (new_uri.empty()) {
			set_failure(ctx, "Failed to ingest grayscale image to VFS.");
			return "Error: Failed to re-ingest grayscale image into VFS cache.";
		}

		set_success(ctx, "Converted image to grayscale");
		return "Successfully converted image to grayscale. New URI: " + new_uri;

	} catch (const Magick::Exception &e) {
		set_failure(ctx, e.what());
		return "GraphicsMagick Error: " + std::string(e.what());
	} catch (const std::exception &e) {
		set_failure(ctx, e.what());
		return "Error: " + std::string(e.what());
	}
}

} // namespace tools
