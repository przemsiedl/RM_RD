#include "RemoteClient.h"
#include "..\Frame.h"
#include <string.h>

RemoteClient:: RemoteClient(const char* _host, int _port) {
	remoteScreenWidth = 0;
   remoteScreenHeight = 0;

    host = new char[strlen(_host) + 1];
    strcpy(host, _host);
    port = _port;

    socket = INVALID_SOCKET;

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(1, 1), &wsaData) != 0) {
        // blad:  nie udalo sie nawiazac polaczenia
    }
}

RemoteClient:: ~RemoteClient() {
    Disconnect();

    if (host) {
        delete[] host;
        host = NULL;
    }

    WSACleanup();
}

void RemoteClient::Init()
{
    LPHOSTENT hostEntry = gethostbyname(host);
    
    if (! hostEntry || ! hostEntry->h_addr_list || !hostEntry->h_addr_list[0]) {
        // blad nie udalo sie rozwiazac nazwy
        return;
    }
}

bool RemoteClient::Connect() {
    if (socket != INVALID_SOCKET) {
        return true;
    }

    socket = :: socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket == INVALID_SOCKET) {
        return false;
    }

    // SOCKET TIMEOUT
    int timeout = 5000;  // 5 sekund
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
    setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));

    // Wylacz algorytm Nagle
    int flag = 1;
    setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(flag));

    // rozwiazanie adresu hosta
    LPHOSTENT hostEntry = gethostbyname(host);
    if (!hostEntry || !hostEntry->h_addr_list || ! hostEntry->h_addr_list[0]) {
        closesocket(socket);
        socket = INVALID_SOCKET;
        return false;
    }
    
    // Przygotuj strukture adresu
    SOCKADDR_IN serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr. sin_port = htons(port);
    memcpy(&serverAddr.sin_addr, hostEntry->h_addr_list[0], sizeof(in_addr));
    
    // Polacz
    if (connect(socket, (PSOCKADDR)&serverAddr, sizeof(serverAddr)) != 0) {
        closesocket(socket);
        socket = INVALID_SOCKET;
        return false;
    }
    
    return true;
}

void RemoteClient::Disconnect() {
    if (socket != INVALID_SOCKET) {
        closesocket(socket);
        socket = INVALID_SOCKET;
    }
}

bool RemoteClient::IsConnected() const {
    return (socket != INVALID_SOCKET);
}

// ===== WYSYLANIE KOMENDY =====
bool RemoteClient::SendCommand(const FrameCmd& cmd, const void* data, int dataSize) {
    if (!Connect()) return false;

    // Wyslij naglowek komendy
    FrameCmd sendCmd = cmd;
    sendCmd.length = dataSize;
    
    if (send(socket, (char*)&sendCmd, sizeof(FrameCmd), 0) != sizeof(FrameCmd)) {
        Disconnect();
        return false;
    }

    // Wyslij dane (jesli sa)
    if (dataSize > 0 && data) {
        if (send(socket, (const char*)data, dataSize, 0) != dataSize) {
            Disconnect();
            return false;
        }
    }

    return true;
}

bool RemoteClient::ReceiveHeader(HeaderBmp& header) {
    if (socket == INVALID_SOCKET) {
        return false;
    }

    int received = recv(socket, (char*)&header, sizeof(HeaderBmp), 0);
    if (received != sizeof(HeaderBmp)) {
        Disconnect();
        return false;
    }

    // Weryfikuj magic
    if (header.magic[0] != 'B' || header.magic[1] != 'M' || header.magic[2] != 'P') {
        return false;
    }

    return true;
}

bool RemoteClient::ReceiveData(char* buffer, int size) {
    if (socket == INVALID_SOCKET || ! buffer || size <= 0) {
        return false;
    }

    int totalReceived = 0;

    while (totalReceived < size) {
        int received = recv(socket,
                           buffer + totalReceived,
                           size - totalReceived,
                           0);

        if (received <= 0) {
            Disconnect();
            return false;
        }

        totalReceived += received;
    }

    return true;
}

bool RemoteClient::Get(Frame& frame) {
    // DataBmp destruktor automatycznie zwolni stare dane
    
    FrameCmd cmd;
    cmd. cmd = CMD_GET_FRAME;

    if (!SendCommand(cmd)) return false;
    if (!ReceiveHeader(frame. header)) return false;

    if (frame.header.length > 0) {
        frame.data.data = new char[frame.header.length];
        if (!frame.data.data) {
            Disconnect();
            return false;
        }

        if (!ReceiveData(frame.data.data, frame. header.length)) {
            delete[] frame.data.data;
            frame.data.data = NULL;
            return false;
        }
    }

    return true;
}

// ===== KOMENDY MYSZY =====

bool RemoteClient::MouseMove(int x, int y) {
    FrameCmd cmd;
    cmd.cmd = CMD_MOUSE_MOVE;
    
    DataMouse data;
    data. x = x;
    data. y = y;
    
    return SendCommand(cmd, &data, sizeof(DataMouse));
}

bool RemoteClient::MouseLeftDown(int x, int y) {
    FrameCmd cmd;
    cmd.cmd = CMD_MOUSE_LEFT_DOWN;
    
    DataMouse data;
    data.x = x;
    data.y = y;
    
    return SendCommand(cmd, &data, sizeof(DataMouse));
}

bool RemoteClient::MouseLeftUp(int x, int y) {
    FrameCmd cmd;
    cmd. cmd = CMD_MOUSE_LEFT_UP;
    
    DataMouse data;
    data. x = x;
    data. y = y;
    
    return SendCommand(cmd, &data, sizeof(DataMouse));
}

bool RemoteClient::MouseRightDown(int x, int y) {
    FrameCmd cmd;
    cmd.cmd = CMD_MOUSE_RIGHT_DOWN;
    
    DataMouse data;
    data.x = x;
    data.y = y;
    
    return SendCommand(cmd, &data, sizeof(DataMouse));
}

bool RemoteClient::MouseRightUp(int x, int y) {
    FrameCmd cmd;
    cmd.cmd = CMD_MOUSE_RIGHT_UP;
    
    DataMouse data;
    data.x = x;
    data.y = y;
    
    return SendCommand(cmd, &data, sizeof(DataMouse));
}

bool RemoteClient::MouseMiddleDown(int x, int y) {
    FrameCmd cmd;
    cmd.cmd = CMD_MOUSE_MIDDLE_DOWN;
    
    DataMouse data;
    data.x = x;
    data.y = y;
    
    return SendCommand(cmd, &data, sizeof(DataMouse));
}

bool RemoteClient::MouseMiddleUp(int x, int y) {
    FrameCmd cmd;
    cmd.cmd = CMD_MOUSE_MIDDLE_UP;
    
    DataMouse data;
    data.x = x;
    data.y = y;
    
    return SendCommand(cmd, &data, sizeof(DataMouse));
}

bool RemoteClient::MouseWheel(int x, int y, int delta) {
    FrameCmd cmd;
    cmd.cmd = CMD_MOUSE_WHEEL;
    
    DataMouse data;
    data.x = x;
    data.y = y;
    data.wheelDelta = delta;
    
    return SendCommand(cmd, &data, sizeof(DataMouse));
}

// ===== KOMENDY KLAWIATURY =====
bool RemoteClient::KeyDown(WORD virtualKey) {
    FrameCmd cmd;
    cmd.cmd = CMD_KEY_DOWN;
    
    DataKey data;
    data.virtualKey = virtualKey;
    data.scanCode = MapVirtualKey(virtualKey, 0);
    data.flags = 0;
    
    return SendCommand(cmd, &data, sizeof(DataKey));
}

bool RemoteClient:: KeyUp(WORD virtualKey) {
    FrameCmd cmd;
    cmd.cmd = CMD_KEY_UP;
    
    DataKey data;
    data.virtualKey = virtualKey;
    data.scanCode = MapVirtualKey(virtualKey, 0);
    data.flags = KEYEVENTF_KEYUP;
    
    return SendCommand(cmd, &data, sizeof(DataKey));
}

bool RemoteClient::FetchBitmap(HBITMAP& result)
{
    Frame frame;    
    if (Get(frame))
	{
        if (hBitmap) {
            DeleteObject(hBitmap);
            hBitmap = NULL;
        }
		
        hBitmap = CreateBitmapFromFrame(frame);
		result = hBitmap;
		return true;
    }
	return false;
	//InvalidateRect(GetActiveWindow(), NULL, TRUE);
}

HBITMAP RemoteClient::CreateBitmapFromFrame(Frame& frame) {
    if (!frame.data.data || frame.header.length <= 0) {
        return NULL;
    }
    
    // Zapamietaj rozmiar zdalnego ekranu
    remoteScreenWidth = frame.header.width;
    remoteScreenHeight = frame.header.height;
    
    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader. biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = frame.header.width;
    bmi.bmiHeader.biHeight = -frame.header.height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 24;
    bmi.bmiHeader.biCompression = BI_RGB;
    
    HDC hdc = GetDC(NULL);
    HBITMAP hBmp = CreateDIBitmap(hdc, &bmi.bmiHeader, CBM_INIT,
                                   frame.data.data, &bmi, DIB_RGB_COLORS);
    ReleaseDC(NULL, hdc);

    return hBmp;
}