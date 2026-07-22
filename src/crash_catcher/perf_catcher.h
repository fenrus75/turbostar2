#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Initializer called automatically when LD_PRELOAD library is loaded
void turboperf_init(void) __attribute__((constructor));

// Destructor called automatically when process terminates
void turboperf_shutdown(void) __attribute__((destructor));

#ifdef __cplusplus
}
#endif
