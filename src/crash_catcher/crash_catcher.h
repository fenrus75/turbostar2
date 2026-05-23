#pragma once

#include <signal.h>

// We must avoid all C++ STL containers and memory allocation here
// to ensure async-signal safety.

#ifdef __cplusplus
extern "C" {
#endif

// The entry point called when a fatal signal occurs
void turbocatch_handle_signal(int sig, siginfo_t* info, void* ucontext);

// Installs the signal handlers when the library is loaded via LD_PRELOAD
void turbocatch_init(void) __attribute__((constructor));

#ifdef __cplusplus
}
#endif