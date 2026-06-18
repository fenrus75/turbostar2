#pragma once

#ifdef HAS_LIBMAGIC
#include <magic.h>
#else
typedef void *magic_t;
#define MAGIC_NONE 0
#define MAGIC_MIME_TYPE 0x000010

inline magic_t magic_open(int)
{
	return nullptr;
}
inline int magic_load(magic_t, const char *)
{
	return -1;
}
inline const char *magic_file(magic_t, const char *)
{
	return nullptr;
}
inline void magic_close(magic_t)
{
}
#endif
