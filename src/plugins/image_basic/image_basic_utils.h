#pragma once

#ifdef HAS_GRAPHICSMAGICK
#include <Magick++.h>

namespace tools
{

inline void set_magick_resource_limits()
{
	Magick::InitializeMagick(nullptr);
	MagickLib::SetMagickResourceLimit(MagickLib::MemoryResource, 128 * 1024 * 1024);
	MagickLib::SetMagickResourceLimit(MagickLib::MapResource, 256 * 1024 * 1024);
	MagickLib::SetMagickResourceLimit(MagickLib::WidthResource, 8192);
	MagickLib::SetMagickResourceLimit(MagickLib::HeightResource, 8192);
}

} // namespace tools
#endif
