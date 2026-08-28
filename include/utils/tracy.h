#pragma once

#if defined(TRACY_ENABLE)
#include <tracy/Tracy.hpp>
#endif

#if defined(TRACY_ENABLE)
#define ENCOS_TRACY_ZONE(name) ZoneScopedN(name)
#define ENCOS_TRACY_FRAME(name) FrameMarkNamed(name)
#define ENCOS_TRACY_LOCKABLE(mutex_type, variable, name) TracyLockableN(mutex_type, variable, name)
#else
#define ENCOS_TRACY_ZONE(name)
#define ENCOS_TRACY_FRAME(name)
#define ENCOS_TRACY_LOCKABLE(mutex_type, variable, name) mutex_type variable
#endif
