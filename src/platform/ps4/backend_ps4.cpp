#include <cstddef>  // orbis/_types/net.h uses size_t without including it

#include <orbis/Net.h>
#include <orbis/Sysmodule.h>
#include <orbis/SystemService.h>

#include <cstdint>

#include "core/log.hpp"
#include "platform/backend.hpp"
#include "platform/language.hpp"
#ifdef PLAYFIN_GPU_PROBE
#include "platform/ps4/gpu_probe_ps4.hpp"
#endif

namespace playfin::platform::backend {

// After SDL_Init, which loads the piglet (OpenGL ES) and system modules the system service needs.
void postInit() {
    sceSystemServiceHideSplashScreen();
    // musl's resolver reads the DNS servers with sceNetGetDnsInfo: without libSceNet loaded, the first host name
    // lookup calls an unresolved stub and the system kills the app (sockets alone go through libkernel)
    if (static_cast<int32_t>(sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET)) < 0 || sceNetInit() < 0) {
        PF_LOG_ERROR("network: cannot load libSceNet, host names will not resolve");
    }
#ifdef PLAYFIN_GPU_PROBE
    ps4::probeGpu();
#endif
}

std::string defaultDataDir() { return "/data/Playfin"; }

std::string systemLanguage() {
    int32_t id = 1;
    if (sceSystemServiceParamGetInt(ORBIS_SYSTEM_SERVICE_PARAM_ID_LANG, &id) < 0) {
        return "en-US";
    }
    return languageFromPs4Id(id);
}

// The console's time zone and its summer time setting (now: a date across a change is an hour off).
int utcOffsetMinutes(std::int64_t utcSeconds) {
    (void)utcSeconds;
    int32_t zone = 0;
    int32_t summer = 0;
    if (sceSystemServiceParamGetInt(ORBIS_SYSTEM_SERVICE_PARAM_ID_TIME_ZONE, &zone) < 0) {
        zone = 0;
    }
    if (sceSystemServiceParamGetInt(ORBIS_SYSTEM_SERVICE_PARAM_ID_SUMMERTIME, &summer) < 0) {
        summer = 0;
    }
    return zone + (summer != 0 ? 60 : 0);
}

std::string resourceDir() { return "/app0/resources"; }

}  // namespace playfin::platform::backend
