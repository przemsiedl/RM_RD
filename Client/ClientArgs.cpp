#include "ClientArgs.h"
#include <stdlib.h>
#include <string.h>

char g_serverHost[256] = "";
int g_serverPort = 0;

bool ParseCommandLine(LPSTR lpCmdLine) {
    if (!lpCmdLine) return false;
    char* p = lpCmdLine;
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) return false;

    // Pierwszy token = host
    char* start = p;
    while (*p && *p != ' ' && *p != '\t') p++;
    size_t hostLen = (size_t)(p - start);
    if (hostLen >= sizeof(g_serverHost)) hostLen = sizeof(g_serverHost) - 1;
    if (hostLen > 0) {
        strncpy(g_serverHost, start, hostLen);
        g_serverHost[hostLen] = '\0';
    } else {
        return false;
    }

    g_serverPort = 8080;  // domyslny port przy jednym argumencie
    while (*p == ' ' || *p == '\t') p++;
    if (*p) {
        int port = atoi(p);
        if (port > 0 && port <= 65535)
            g_serverPort = port;
    }
    return true;
}
