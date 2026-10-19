#include "Config.h"
#include <windows.h>
#include <string.h>

static int ConfigPath(char* exePath) {
    if (GetModuleFileNameA(NULL, exePath, MAX_PATH) == 0)
        return 0;
    char* lastSlash = strrchr(exePath, '\\');
    if (lastSlash)
        *(lastSlash + 1) = '\0';
    strcat(exePath, "config.ini");
    return 1;
}

int LoadPortFromConfig() {
    char exePath[MAX_PATH];
    if (!ConfigPath(exePath))
        return 8080;
    return GetPrivateProfileIntA("Server", "Port", 8080, exePath);
}

int LoadBitsPerPixelFromConfig() {
    char exePath[MAX_PATH];
    if (!ConfigPath(exePath))
        return 24;
    int bpp = GetPrivateProfileIntA("Server", "Bpp", 24, exePath);
    if (bpp != 16 && bpp != 24 && bpp != 32)
        return 24;
    return bpp;
}
