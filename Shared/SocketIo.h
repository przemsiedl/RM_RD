#ifndef SocketIoH
#define SocketIoH

#include <windows.h>
#include <winsock.h>

namespace SocketIo {

bool SendAll(SOCKET s, const char* data, int size);
bool RecvAll(SOCKET s, char* buffer, int size);

}

#endif
