#include "Config.h"
#include <windows.h>
#include <string.h>

int LoadPortFromConfig() {
    char exePath[MAX_PATH];
    if (GetModuleFileNameA(NULL, exePath, MAX_PATH) == 0)
        return 8080;
    char* lastSlash = strrchr(exePath, '\\');
    if (lastSlash)
        *(lastSlash + 1) = '\0';
    strcat(exePath, "config.ini");
    return GetPrivateProfileIntA("Server", "Port", 8080, exePath);
}
