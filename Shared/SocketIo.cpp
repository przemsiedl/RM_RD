<<<<<<< Updated upstream
#include "SocketIo.h"
=======
#include "..\Shared\SocketIo.h"
>>>>>>> Stashed changes

bool SocketIo::SendAll(SOCKET s, const char* data, int size) {
    if (!data || size <= 0) {
        return true;
    }
    int totalSent = 0;
    while (totalSent < size) {
        int sent = send(s, data + totalSent, size - totalSent, 0);
        if (sent <= 0) {
            return false;
        }
        totalSent += sent;
    }
    return true;
}

bool SocketIo::RecvAll(SOCKET s, char* buffer, int size) {
    if (!buffer || size <= 0) {
        return true;
    }
    int totalReceived = 0;
    while (totalReceived < size) {
        int received = recv(s, buffer + totalReceived, size - totalReceived, 0);
        if (received <= 0) {
            return false;
        }
        totalReceived += received;
    }
    return true;
}
