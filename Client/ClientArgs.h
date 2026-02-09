#ifndef ClientArgsH
#define ClientArgsH

#include <windows.h>

// Adres i port serwera po parsowaniu argumentow (wypelniane przez ParseCommandLine)
extern char g_serverHost[256];
extern int g_serverPort;

// Parsuje lpCmdLine (bez nazwy programu) na host i port.
// Zwraca true, jesli podano przynajmniej host.
bool ParseCommandLine(LPSTR lpCmdLine);

#endif
