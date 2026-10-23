#pragma once
// included before every host source by CMake, as Black-Android's native/compat/crt.h: what the SDK's portable
// sources take from the C library without including it themselves, and what newlib lacks
#include <pthread.h>
#include <unistd.h>

// newlib has no thread names: the runtime and the render thread name theirs for debuggers only
inline int pthread_setname_np(pthread_t, const char*) { return 0; }
