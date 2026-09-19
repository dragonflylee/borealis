/*
    Runtime resolution of the asset base directory.

    BRLS_ASSET used to concatenate the compile-time BRLS_RESOURCES literal, which
    on a sandboxed PS5 build is "/app0/resources/". That path only exists inside
    the title jail; a process that escapes the sandbox must resolve assets at
    their real location instead. The base is therefore a runtime value, seeded
    from BRLS_RESOURCES so a build that never escapes behaves exactly as before.
*/
#include <borealis/core/assets.hpp>

#ifdef PS5_NATIVE_GPU
#include <utility>

namespace brls {

static std::string& baseRef() {
    static std::string base = BRLS_RESOURCES;
    return base;
}

const std::string& resourceBase() { return baseRef(); }

void setResourceBase(std::string base) { baseRef() = std::move(base); }

std::string resourcePath(const std::string& relative) { return baseRef() + relative; }

}  // namespace brls

#endif
