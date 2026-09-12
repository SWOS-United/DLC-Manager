// gl_core33.cpp -- resolve the GL 3.3 core subset declared in gl_core33.h.

#include "gl_core33.h"

#include <SDL.h>

#define SWOS_GL_DEFINE(ret, name, args) ret(GL_APIENTRY *gl##name) args = nullptr;
SWOS_GL_FUNCTIONS(SWOS_GL_DEFINE)
#undef SWOS_GL_DEFINE

namespace {
const char *s_missing = nullptr;
}

namespace gl {

bool load()
{
    s_missing = nullptr;

#define SWOS_GL_LOAD(ret, name, args)                                                              \
    do {                                                                                           \
        gl##name = reinterpret_cast<ret(GL_APIENTRY *) args>(SDL_GL_GetProcAddress("gl" #name));    \
        if (!gl##name && !s_missing) s_missing = "gl" #name;                                       \
    } while (0);
    SWOS_GL_FUNCTIONS(SWOS_GL_LOAD)
#undef SWOS_GL_LOAD

    return s_missing == nullptr;
}

const char *missing_symbol()
{
    return s_missing;
}

} // namespace gl
