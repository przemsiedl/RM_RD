#include "RemoteClient.h"
#include "..\Shared\Frame.h"
#include <string.h>

RemoteClient::RemoteClient(const char* _host, int _port) {
    remoteScreenWidth = 0;
    remoteScreenHeight = 0;
    
    pFrameBuffer = NULL;
    hBitmap = NULL;

    host = new char[strlen(_host) + 1];
    strcpy(host, _host);
    port = _port;

    socket = INVALID_SOCKET;

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(1, 1), &wsaData) != 0) {
        // blad:  nie udalo sie nawiazac polaczenia
    }
}

RemoteClient::~RemoteClient() {
    Disconnect();
    
    // Zwolnij bufor (ImageData destruktor zwalnia pData)
    if (pFrameBuffer) {
        delete pFrameBuffer;
        pFrameBuffer = NULL;
    }
    
    if (hBitmap) {
        DeleteObject(hBitmap);
        hBitmap = NULL;
    }

    if (host) {
        delete[] host;
        host = NULL;
    }

    WSACleanup();
}

void RemoteClient::Init()
{
    LPHOSTENT hostEntry = gethostbyname(host);
    
    if (! hostEntry || !hostEntry->h_addr_list || !hostEntry->h_addr_list[0]) {
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
    if (!hostEntry || !hostEntry->h_addr_list || !hostEntry->h_addr_list[0]) {
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

// ===== ZAPEWNIENIE BUFORA (ALOKACJA/REALOKACJA) =====
bool RemoteClient::EnsureFrameBuffer(int width, int height, int bpp, int stride) {
    DWORD newSize = stride * height;
    
    // Sprawdź czy trzeba realokować
    bool needsRealloc = (! pFrameBuffer ||
                         pFrameBuffer->width != width || 
                         pFrameBuffer->height != height ||
                         pFrameBuffer->bitsPerPixel != bpp ||
                         pFrameBuffer->stride != stride);
    
    if (needsRealloc) {
        // Zwolnij stary bufor
        if (pFrameBuffer) {
            delete pFrameBuffer;
        }
        
        // Utwórz nowy
        pFrameBuffer = new ImageData();
        if (!pFrameBuffer) {
            return false;
        }
        
        pFrameBuffer->width = width;
        pFrameBuffer->height = height;
        pFrameBuffer->bitsPerPixel = bpp;
        pFrameBuffer->stride = stride;
        pFrameBuffer->dataSize = newSize;
        pFrameBuffer->pData = new BYTE[newSize];
        pFrameBuffer->isFullFrame = true;
        
        if (!pFrameBuffer->pData) {
            delete pFrameBuffer;
            pFrameBuffer = NULL;
            return false;
        }
        
        // Wyzeruj bufor
        memset(pFrameBuffer->pData, 0, newSize);
    }
    
    return true;
}

// ===== ODBIERANIE DANYCH BEZPOŚREDNIO DO ImageData =====
bool RemoteClient:: ReceiveImageData(ImageData* img, const HeaderBmp& header) {
    if (!img || !img->pData || socket == INVALID_SOCKET) {
        return false;
    }
    
    // Sprawdź flagę - pełna klatka czy różnica? 
    bool isFullFrame = (header.flags & FRAME_FLAG_FULL) != 0;
    
    // Alokuj tymczasowy bufor dla odebranych danych
    BYTE* tempBuffer = new BYTE[header.length];
    if (!tempBuffer) {
        return false;
    }
    
    // Odbierz dane z sieci
    int totalReceived = 0;
    while (totalReceived < header.length) {
        int received = recv(socket,
                           (char*)(tempBuffer + totalReceived),
                           header.length - totalReceived,
                           0);
        
        if (received <= 0) {
            delete[] tempBuffer;
            Disconnect();
            return false;
        }
        
        totalReceived += received;
    }
    
    // ===== APLIKUJ DANE NA BUFOR =====
    if (isFullFrame) {
        // Pełny obraz - skopiuj bezpośrednio
        memcpy(img->pData, tempBuffer, img->dataSize);
        img->isFullFrame = true;
    } else {
        // Różnica XOR - aplikuj przy użyciu BmpStream
        int bytesPerPixel = img->bitsPerPixel / 8;
        
        for (int y = 0; y < img->height; y++) {
            const BYTE* pRecvRow = tempBuffer + (y * img->stride);
            BYTE* pBufferRow = img->pData + (y * img->stride);
            
            for (int x = 0; x < img->width; x++) {
                int offset = x * bytesPerPixel;
                
                // XOR w miejscu
                for (int i = 0; i < bytesPerPixel; i++) {
                    pBufferRow[offset + i] ^= pRecvRow[offset + i];
                }
            }
        }
        
        img->isFullFrame = false;
    }
    
    // Zwolnij tymczasowy bufor
    delete[] tempBuffer;
    
    return true;
}

// ===== WYSYŁANIE KOMENDY =====
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

// ===== POBIERANIE BITMAPY (UPROSZCZONE) =====
bool RemoteClient::FetchBitmap(HBITMAP& result) {
    // Wyślij komendę GET_FRAME
    FrameCmd cmd;
    cmd.cmd = CMD_GET_FRAME;
    
    if (!SendCommand(cmd)) {
        return false;
    }
    
    // Odbierz nagłówek
    HeaderBmp header;
    int received = recv(socket, (char*)&header, sizeof(HeaderBmp), 0);
    if (received != sizeof(HeaderBmp)) {
        Disconnect();
        return false;
    }
    
    // Weryfikuj magic
    if (header.magic[0] != 'B' || header.magic[1] != 'M' || header.magic[2] != 'P') {
        return false;
    }
    
    // Zapewnij bufor odpowiedniego rozmiaru
    if (!EnsureFrameBuffer(header. width, header.height, 
                          header.bitsPerPixel, header.stride)) {
        return false;
    }
    
    // Odbierz dane bezpośrednio do bufora
    if (!ReceiveImageData(pFrameBuffer, header)) {
        return false;
    }
    
    // Zapamietaj rozmiar zdalnego ekranu
    remoteScreenWidth = pFrameBuffer->width;
    remoteScreenHeight = pFrameBuffer->height;
    
    // Utwórz bitmapę z bufora
    if (hBitmap) {
        DeleteObject(hBitmap);
        hBitmap = NULL;
    }
    
    hBitmap = CreateBitmapFromImageData(pFrameBuffer);
    result = hBitmap;
    
    return (hBitmap != NULL);
}

// ===== TWORZENIE BITMAPY Z ImageData =====
HBITMAP RemoteClient::CreateBitmapFromImageData(const ImageData* img) {
    if (!img || !img->pData || img->width <= 0 || img->height <= 0) {
        return NULL;
    }
    
    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = img->width;
    bmi.bmiHeader.biHeight = -img->height;  // Top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = img->bitsPerPixel;
    bmi.bmiHeader.biCompression = BI_RGB;
    
    HDC hdc = GetDC(NULL);
    HBITMAP hBmp = CreateDIBitmap(hdc, &bmi.bmiHeader, CBM_INIT,
                                   img->pData, &bmi, DIB_RGB_COLORS);
    ReleaseDC(NULL, hdc);

    return hBmp;
}

// ===== KOMENDY MYSZY =====

bool RemoteClient::MouseMove(int x, int y) {
    FrameCmd cmd;
    cmd.cmd = CMD_MOUSE_MOVE;
    
    DataMouse data;
    data.x = x;
    data.y = y;
    
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
    cmd.cmd = CMD_MOUSE_LEFT_UP;
    
    DataMouse data;
    data.x = x;
    data.y = y;
    
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

bool RemoteClient:: MouseMiddleDown(int x, int y) {
    FrameCmd cmd;
    cmd. cmd = CMD_MOUSE_MIDDLE_DOWN;
    
    DataMouse data;
    data.x = x;
    data.y = y;
    
    return SendCommand(cmd, &data, sizeof(DataMouse));
}

bool RemoteClient:: MouseMiddleUp(int x, int y) {
    FrameCmd cmd;
    cmd. cmd = CMD_MOUSE_MIDDLE_UP;
    
    DataMouse data;
    data.x = x;
    data.y = y;
    
    return SendCommand(cmd, &data, sizeof(DataMouse));
}

bool RemoteClient:: MouseWheel(int x, int y, int delta) {
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

bool RemoteClient::KeyUp(WORD virtualKey) {
    FrameCmd cmd;
    cmd.cmd = CMD_KEY_UP;
    
    DataKey data;
    data.virtualKey = virtualKey;
    data.scanCode = MapVirtualKey(virtualKey, 0);
    data.flags = KEYEVENTF_KEYUP;
    
    return SendCommand(cmd, &data, sizeof(DataKey));
}