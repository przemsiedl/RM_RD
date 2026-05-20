#include "RemoteClient.h"
#include "..\Shared\Frame.h"
#include "..\Shared\Compression.h"
#include "..\Shared\BmpStream.h"
#include "..\Shared\SocketIo.h"
#include <string.h>

RemoteClient::RemoteClient(const char* _host, int _port) {
    diffBlockW = 32;
    diffBlockH = 32;
    diffGridDirty = true;

    remoteScreenWidth = 0;
    remoteScreenHeight = 0;
    
    pFrameBuffer = NULL;
    hBitmap = NULL;
    bitmapWidth = 0;
    bitmapHeight = 0;

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
        bitmapWidth = 0;
        bitmapHeight = 0;
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

void RemoteClient::SetDiffGrid(int blockW, int blockH) {
    diffBlockW = blockW;
    diffBlockH = blockH;
    diffGridDirty = true;
}

bool RemoteClient::ReceiveFrameHeader(HeaderBmp& header) {
    return SocketIo::RecvAll(socket, (char*)&header, sizeof(HeaderBmp));
}

bool RemoteClient::ValidateFrameHeader(const HeaderBmp& header) const {
    return (header.magic[0] == 'B' && header.magic[1] == 'M' && header.magic[2] == 'P');
}

bool RemoteClient::HandleNoChangeFrame(const HeaderBmp& header, HBITMAP& result) {
    if ((header.flags & FRAME_FLAG_NOCHANGE) == 0) {
        return false;
    }
    /* TRANSLATION + NOCHANGE: 8 B dx,dy w ReceiveImageData */
    if ((header.flags & FRAME_FLAG_TRANSLATION) != 0) {
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

// ===== ODBIERANIE DANYCH BEZPOŚREDNIO DO ImageData =====
bool RemoteClient::ReceiveImageData(ImageData* img, const HeaderBmp& header) {
    if (!img || !img->pData || socket == INVALID_SOCKET) {
        return false;
    }

    bool isFullFrame = (header.flags & FRAME_FLAG_FULL) != 0;
    bool isCompressed = (header.flags & FRAME_FLAG_COMPRESSED) != 0;
    bool isNoChange = (header.flags & FRAME_FLAG_NOCHANGE) != 0;
    bool isBlocks = (header.flags & FRAME_FLAG_BLOCKS) != 0;
    bool isTrans = (header.flags & FRAME_FLAG_TRANSLATION) != 0;

    if (isNoChange) {
        if (isTrans) {
            if (header.length != 8) return false;
            char prefix[8];
            if (!SocketIo::RecvAll(socket, prefix, 8)) {
                Disconnect();
                return false;
            }
            int dx, dy;
            memcpy(&dx, prefix, 4);
            memcpy(&dy, prefix + 4, 4);
            if (!BmpStream::ApplyTranslationResidual(img, dx, dy, NULL, 0, 0, 0))
                return false;
            img->isFullFrame = false;
            return true;
        }
        if (header.length != 0) return false;
        return true;
    }

    if (header.length <= 0) return false;

    if (isFullFrame && isTrans) {
        /* Pelna klatka bez prefiksu translacji w protokole */
        return false;
    }

    char* receivedBuffer = new char[header.length];
    if (!receivedBuffer) return false;

    if (!SocketIo::RecvAll(socket, receivedBuffer, header.length)) {
        delete[] receivedBuffer;
        Disconnect();
        return false;
    }

    int transDx = 0, transDy = 0;
    const char* payloadStart = receivedBuffer;
    int payloadLen = header.length;
    if (isTrans) {
        if (payloadLen < 8) {
            delete[] receivedBuffer;
            return false;
        }
        memcpy(&transDx, payloadStart, 4);
        memcpy(&transDy, payloadStart + 4, 4);
        payloadStart += 8;
        payloadLen -= 8;
    }

    if (!isFullFrame && isBlocks) {
        if (!isCompressed) {
            delete[] receivedBuffer;
            return false;
        }
        if (payloadLen < 4) {
            delete[] receivedBuffer;
            return false;
        }
        if (isTrans) {
            if (!BmpStream::ApplyTranslationResidual(img, transDx, transDy, NULL, 0, 0, 0)) {
                delete[] receivedBuffer;
                return false;
            }
        }
        if (!BmpStream::ApplyPackedBlockDiffs(img, payloadStart, payloadLen)) {
            delete[] receivedBuffer;
            return false;
        }
        img->isFullFrame = false;
        delete[] receivedBuffer;
        return true;
    }

    int expectedDataSize = (int)img->dataSize;

    char* workingBuffer = NULL;
    int workingSize = 0;

    if (isCompressed) {
        int maxDecompressed = expectedDataSize + 4096;
        workingBuffer = new char[maxDecompressed];
        if (!workingBuffer) {
            delete[] receivedBuffer;
            return false;
        }
        Compression::decrypt(payloadStart, payloadLen, workingBuffer, workingSize);
        if (workingSize != expectedDataSize) {
            delete[] receivedBuffer;
            delete[] workingBuffer;
            return false;
        }
    } else {
        workingBuffer = (char*)payloadStart;
        workingSize = payloadLen;
        if (workingSize != expectedDataSize) {
            delete[] receivedBuffer;
            return false;
        }
    }

    if (isFullFrame) {
        memcpy(img->pData, workingBuffer, img->dataSize);
        img->isFullFrame = true;
    } else {
        if (isTrans) {
            if (!BmpStream::ApplyTranslationResidual(img, transDx, transDy,
                    workingBuffer, img->stride, img->width, img->height)) {
                delete[] receivedBuffer;
                if (isCompressed) delete[] workingBuffer;
                return false;
            }
        } else {
            if (!BmpStream::ApplyDiff(img->pData, img->stride, img->bitsPerPixel,
                    workingBuffer, img->stride, img->width, img->height, 0, 0, false)) {
                delete[] receivedBuffer;
                if (isCompressed) delete[] workingBuffer;
                return false;
            }
        }
        img->isFullFrame = false;
    }

    delete[] receivedBuffer;
    if (isCompressed) delete[] workingBuffer;
    return true;
}

// ===== WYSYŁANIE KOMENDY =====
bool RemoteClient::SendCommand(const FrameCmd& cmd, const void* data, int dataSize) {
    if (!Connect()) return false;

    FrameCmd sendCmd = cmd;
    sendCmd.length = dataSize;

    if (!SocketIo::SendAll(socket, (char*)&sendCmd, sizeof(FrameCmd))) {
        Disconnect();
        return false;
    }
    if (dataSize > 0 && data) {
        if (!SocketIo::SendAll(socket, (const char*)data, dataSize)) {
            Disconnect();
            return false;
        }
    }
    return true;
}

// ===== POBIERANIE BITMAPY (UPROSZCZONE) =====
bool RemoteClient::FetchBitmap(HBITMAP& result) {
    if (diffGridDirty) {
        DataDiffGrid dg;
        dg.blockW = diffBlockW;
        dg.blockH = diffBlockH;
        FrameCmd gridCmd;
        gridCmd.cmd = CMD_SET_DIFF_GRID;
        if (!SendCommand(gridCmd, &dg, sizeof(dg))) {
            return false;
        }
        diffGridDirty = false;
    }

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
    
    int bufWidth = header.width;
    int bufHeight = header.height;
    if ((header.flags & FRAME_FLAG_BLOCKS) && pFrameBuffer) {
        bufWidth = pFrameBuffer->width;
        bufHeight = pFrameBuffer->height;
    }
    int bufStride = (header.stride > 0) ? header.stride : ((bufWidth * header.bitsPerPixel + 31) / 32) * 4;
    if (!EnsureFrameBuffer(bufWidth, bufHeight, header.bitsPerPixel, bufStride)) {
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
    
    remoteScreenWidth = pFrameBuffer->width;
    remoteScreenHeight = pFrameBuffer->height;

    if (!CreateOrUpdateBitmap(pFrameBuffer)) {
        return false;
    }
    result = hBitmap;
    return true;
}

// ===== TWORZENIE LUB AKTUALIZACJA BITMAPY (REUSE via SetDIBits) =====
bool RemoteClient::CreateOrUpdateBitmap(const ImageData* img) {
    if (!img || !img->pData || img->width <= 0 || img->height <= 0) {
        return false;
    }

    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = img->width;
    bmi.bmiHeader.biHeight = -img->height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = img->bitsPerPixel;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC hdc = GetDC(NULL);
    if (!hdc) return false;

    if (hBitmap && bitmapWidth == img->width && bitmapHeight == img->height) {
        int lines = SetDIBits(hdc, hBitmap, 0, img->height, img->pData, &bmi, DIB_RGB_COLORS);
        ReleaseDC(NULL, hdc);
        return (lines == img->height);
    }

    if (hBitmap) {
        DeleteObject(hBitmap);
        hBitmap = NULL;
    }

    hBitmap = CreateDIBitmap(hdc, &bmi.bmiHeader, CBM_INIT, img->pData, &bmi, DIB_RGB_COLORS);
    ReleaseDC(NULL, hdc);

    if (!hBitmap) return false;

    bitmapWidth = img->width;
    bitmapHeight = img->height;
    return true;
}
