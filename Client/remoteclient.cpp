#include "RemoteClient.h"
#include "..\Shared\Frame.h"
#include "..\Shared\Compression.h"
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

bool RemoteClient::ReceiveFrameHeader(HeaderBmp& header) {
    int received = recv(socket, (char*)&header, sizeof(HeaderBmp), 0);
    return (received == sizeof(HeaderBmp));
}

bool RemoteClient::ValidateFrameHeader(const HeaderBmp& header) const {
    return (header.magic[0] == 'B' && header.magic[1] == 'M' && header.magic[2] == 'P');
}

bool RemoteClient::HandleNoChangeFrame(const HeaderBmp& header, HBITMAP& result) {
    if ((header.flags & FRAME_FLAG_NOCHANGE) == 0) {
        return false;
    }
    if (header.length != 0 || !hBitmap) {
        return false;
    }
    result = hBitmap;
    return true;
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
        pFrameBuffer->pData = new char[newSize];
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

// ===== ODBIERANIE DANYCH BEZPOŚREDNIO DO ImageData (POPRAWIONE) =====
bool RemoteClient::ReceiveImageData(ImageData* img, const HeaderBmp& header) {
    if (!img || !img->pData || socket == INVALID_SOCKET) {
        return false;
    }
    
    // Sprawdź flagi
    bool isFullFrame = (header.flags & FRAME_FLAG_FULL) != 0;
    bool isCompressed = (header.flags & FRAME_FLAG_COMPRESSED) != 0;
    bool isNoChange = (header.flags & FRAME_FLAG_NOCHANGE) != 0;

    if (isNoChange) {
        if (header.length != 0) {
            return false;
        }
        return true;
    }

    if (header.length <= 0) {
        return false;
    }
    
    // Alokuj tymczasowy bufor dla odebranych danych
    char* receivedBuffer = new char[header.length];
    if (!receivedBuffer) {
        return false;
    }
    
    // Odbierz dane z sieci
    int totalReceived = 0;
    while (totalReceived < header. length) {
        int received = recv(socket,
                           receivedBuffer + totalReceived,
                           header. length - totalReceived,
                           0);
        
        if (received <= 0) {
            delete[] receivedBuffer;
            Disconnect();
            return false;
        }
        
        totalReceived += received;
    }

    // ===== DEKOMPRESJA (jeśli dane są skompresowane) =====
    char* workingBuffer = NULL;
    int workingSize = 0;
    
    if (isCompressed) {
        // Alokuj bufor na zdekompresowane dane
        workingBuffer = new char[img->dataSize];
        if (! workingBuffer) {
            delete[] receivedBuffer;
            return false;
        }
        
        // Dekompresuj
        Compression::decrypt(receivedBuffer, header.length, workingBuffer, workingSize);
        
        // Sprawdź czy rozmiar się zgadza
        if (workingSize != img->dataSize) {
            delete[] receivedBuffer;
            delete[] workingBuffer;
            return false;
        }
    } else {
        // Dane nieskompresowane - użyj bezpośrednio
        workingBuffer = receivedBuffer;
        workingSize = header.length;
        if (workingSize != img->dataSize) {
            delete[] receivedBuffer;
            return false;
        }
    }

    // ===== APLIKUJ DANE NA BUFOR =====
    if (isFullFrame) {
        // Pełna klatka - po prostu kopiuj
        memcpy(img->pData, workingBuffer, img->dataSize);
        img->isFullFrame = true;
    } else {
        // Różnica XOR - aplikuj w miejscu
        ImageData diffView;
        diffView.width = img->width;
        diffView.height = img->height;
        diffView.bitsPerPixel = img->bitsPerPixel;
        diffView.stride = img->stride;
        diffView.dataSize = img->dataSize;
        diffView.pData = workingBuffer;
        diffView.isFullFrame = false;
        diffView.isEmptyDiff = false;

        if (!BmpStream::ApplyDiffXOR(img->pData, &diffView)) {
            diffView.pData = NULL;
            delete[] receivedBuffer;
            if (isCompressed) {
                delete[] workingBuffer;
            }
            return false;
        }

        diffView.pData = NULL;
        img->isFullFrame = false;
    }
    
    // Zwolnij bufory
    delete[] receivedBuffer;
    if (isCompressed) {
        delete[] workingBuffer;
    }
    
    return true;
}

// ===== WYSYŁANIE KOMENDY =====
bool RemoteClient::SendCommand(const FrameCmd& cmd, const void* data, int dataSize) {
    if (! Connect()) return false;

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
    if (!ReceiveFrameHeader(header)) {
        Disconnect();
        return false;
    }
    
    // Weryfikuj magic
    if (!ValidateFrameHeader(header)) {
        return false;
    }
    
    // Zapewnij bufor odpowiedniego rozmiaru
    if (!EnsureFrameBuffer(header.width, header.height, 
                          header.bitsPerPixel, header.stride)) {
        return false;
    }

    if (HandleNoChangeFrame(header, result)) {
        remoteScreenWidth = pFrameBuffer->width;
        remoteScreenHeight = pFrameBuffer->height;
        return true;
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
