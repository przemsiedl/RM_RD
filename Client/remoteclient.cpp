#include "RemoteClient.h"
#include "../Shared/Frame.h"
#include "../Shared/compression.h"
#include "../Shared/BmpStream.h"
#include "../Shared/SocketIo.h"
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
    recvScratch = NULL;
    recvCap = 0;
    decScratch = NULL;
    decCap = 0;
    lastChanged = false;
    inputHead = 0;
    inputCount = 0;
    InitializeCriticalSection(&queueSection);
    InitializeCriticalSection(&frameSection);
    inputEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

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
    
    /* Bity obrazu naleza do DIB, nie do ImageData. */
    if (pFrameBuffer) {
        pFrameBuffer->pData = NULL;
        delete pFrameBuffer;
        pFrameBuffer = NULL;
    }
    
    if (hBitmap) {
        DeleteObject(hBitmap);
        hBitmap = NULL;
        bitmapWidth = 0;
        bitmapHeight = 0;
    }

    if (recvScratch) {
        delete[] recvScratch;
        recvScratch = NULL;
    }
    if (decScratch) {
        delete[] decScratch;
        decScratch = NULL;
    }
    if (inputEvent) {
        CloseHandle(inputEvent);
        inputEvent = NULL;
    }
    DeleteCriticalSection(&queueSection);
    DeleteCriticalSection(&frameSection);

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

void RemoteClient::LockFrame() {
    EnterCriticalSection(&frameSection);
}

void RemoteClient::UnlockFrame() {
    LeaveCriticalSection(&frameSection);
}

bool RemoteClient::GetLastDirty(int& x, int& y, int& w, int& h) const {
    if (!lastChanged || !lastDirty.any) {
        x = y = w = h = 0;
        return false;
    }
    x = lastDirty.minX;
    y = lastDirty.minY;
    w = lastDirty.Width();
    h = lastDirty.Height();
    return true;
}

void RemoteClient::EnqueueInput(const RmInputEvent& ev) {
    EnterCriticalSection(&queueSection);
    if (!ev.isKey && ev.cmd == CMD_MOUSE_MOVE && inputCount > 0) {
        int last = (inputHead + inputCount - 1) % RM_INPUT_QUEUE;
        if (!inputQueue[last].isKey && inputQueue[last].cmd == CMD_MOUSE_MOVE) {
            inputQueue[last].x = ev.x;
            inputQueue[last].y = ev.y;
            inputQueue[last].wheel = ev.wheel;
            LeaveCriticalSection(&queueSection);
            if (inputEvent)
                SetEvent(inputEvent);
            return;
        }
    }
    if (inputCount >= RM_INPUT_QUEUE) {
        inputHead = (inputHead + 1) % RM_INPUT_QUEUE;
        inputCount--;
    }
    inputQueue[(inputHead + inputCount) % RM_INPUT_QUEUE] = ev;
    inputCount++;
    LeaveCriticalSection(&queueSection);
    if (inputEvent)
        SetEvent(inputEvent);
}

void RemoteClient::QueueMouse(int cmd, int x, int y, int wheel) {
    RmInputEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.cmd = cmd;
    ev.isKey = 0;
    ev.x = x;
    ev.y = y;
    ev.wheel = wheel;
    EnqueueInput(ev);
}

void RemoteClient::QueueKey(int cmd, WORD virtualKey, WORD scanCode, DWORD flags) {
    RmInputEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.cmd = cmd;
    ev.isKey = 1;
    ev.virtualKey = virtualKey;
    ev.scanCode = scanCode;
    ev.flags = flags;
    EnqueueInput(ev);
}

bool RemoteClient::HasPendingInput() {
    EnterCriticalSection(&queueSection);
    bool pending = (inputCount > 0);
    LeaveCriticalSection(&queueSection);
    return pending;
}

bool RemoteClient::FlushInput() {
    RmInputEvent local[RM_INPUT_QUEUE];
    int n = 0;
    EnterCriticalSection(&queueSection);
    n = inputCount;
    for (int i = 0; i < n; i++)
        local[i] = inputQueue[(inputHead + i) % RM_INPUT_QUEUE];
    inputHead = 0;
    inputCount = 0;
    LeaveCriticalSection(&queueSection);

    for (int i = 0; i < n; i++) {
        FrameCmd cmd;
        cmd.cmd = local[i].cmd;
        if (local[i].isKey) {
            DataKey key;
            key.virtualKey = local[i].virtualKey;
            key.scanCode = local[i].scanCode;
            key.flags = local[i].flags;
            if (!SendCommand(cmd, &key, sizeof(key)))
                return false;
        } else {
            DataMouse mouse;
            mouse.x = local[i].x;
            mouse.y = local[i].y;
            mouse.wheelDelta = local[i].wheel;
            if (!SendCommand(cmd, &mouse, sizeof(mouse)))
                return false;
        }
    }
    return true;
}

char* RemoteClient::GrowScratch(char*& buf, int& cap, int need) {
    if (need <= 0)
        return NULL;
    if (cap < need) {
        delete[] buf;
        buf = new char[need];
        cap = need;
    }
    return buf;
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

// ===== ZAPEWNIENIE BUFORA (DIB section, bity pisane w miejscu) =====
bool RemoteClient::EnsureFrameBuffer(int width, int height, int bpp, int stride) {
    int canonical = ((width * bpp + 31) / 32) * 4;
    if (stride <= 0)
        stride = canonical;
    if (stride != canonical)
        return false;

    if (pFrameBuffer && hBitmap &&
        pFrameBuffer->width == width &&
        pFrameBuffer->height == height &&
        pFrameBuffer->bitsPerPixel == bpp &&
        pFrameBuffer->stride == stride) {
        return true;
    }

    if (hBitmap) {
        DeleteObject(hBitmap);
        hBitmap = NULL;
    }
    if (pFrameBuffer) {
        pFrameBuffer->pData = NULL;
        delete pFrameBuffer;
        pFrameBuffer = NULL;
    }

    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = (WORD)bpp;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC hdc = GetDC(NULL);
    if (!hdc)
        return false;
    void* bits = NULL;
    hBitmap = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, hdc);
    if (!hBitmap || !bits) {
        if (hBitmap) {
            DeleteObject(hBitmap);
            hBitmap = NULL;
        }
        return false;
    }

    DWORD newSize = (DWORD)stride * (DWORD)height;
    pFrameBuffer = new ImageData();
    pFrameBuffer->width = width;
    pFrameBuffer->height = height;
    pFrameBuffer->bitsPerPixel = bpp;
    pFrameBuffer->stride = stride;
    pFrameBuffer->dataSize = (long)newSize;
    pFrameBuffer->pData = (char*)bits;
    pFrameBuffer->isFullFrame = true;
    memset(pFrameBuffer->pData, 0, newSize);
    bitmapWidth = width;
    bitmapHeight = height;
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
            LockFrame();
            bool ok = BmpStream::ApplyTranslationResidual(img, dx, dy, NULL, 0, 0, 0);
            if (ok) {
                img->isFullFrame = false;
                lastDirty.SetFull(img->width, img->height);
                lastChanged = true;
            }
            UnlockFrame();
            return ok;
        }
        if (header.length != 0) return false;
        lastChanged = false;
        lastDirty.Clear();
        return true;
    }

    if (header.length <= 0) return false;

    if (isFullFrame && isTrans) {
        /* Pelna klatka bez prefiksu translacji w protokole */
        return false;
    }

    char* receivedBuffer = GrowScratch(recvScratch, recvCap, header.length);
    if (!receivedBuffer) return false;

    if (!SocketIo::RecvAll(socket, receivedBuffer, header.length)) {
        Disconnect();
        return false;
    }

    int transDx = 0, transDy = 0;
    const char* payloadStart = receivedBuffer;
    int payloadLen = header.length;
    if (isTrans) {
        if (payloadLen < 8)
            return false;
        memcpy(&transDx, payloadStart, 4);
        memcpy(&transDy, payloadStart + 4, 4);
        payloadStart += 8;
        payloadLen -= 8;
    }

    if (!isFullFrame && isBlocks) {
        if (!isCompressed)
            return false;
        if (payloadLen < 4)
            return false;
        LockFrame();
        bool ok = true;
        if (isTrans)
            ok = BmpStream::ApplyTranslationResidual(img, transDx, transDy, NULL, 0, 0, 0);
        FrameDirty blockDirty;
        if (ok)
            ok = BmpStream::ApplyPackedBlockDiffs(img, payloadStart, payloadLen, &blockDirty);
        if (ok) {
            img->isFullFrame = false;
            if (isTrans)
                lastDirty.SetFull(img->width, img->height);
            else
                lastDirty = blockDirty;
            lastChanged = lastDirty.any;
        }
        UnlockFrame();
        return ok;
    }

    int expectedDataSize = (int)img->dataSize;

    char* workingBuffer = NULL;
    int workingSize = 0;

    bool usedDecrypt = false;
    if (isCompressed) {
        int maxDecompressed = expectedDataSize + 4096;
        workingBuffer = GrowScratch(decScratch, decCap, maxDecompressed);
        if (!workingBuffer)
            return false;
        usedDecrypt = true;
        Compression::decrypt(payloadStart, payloadLen, workingBuffer, workingSize);
        if (workingSize != expectedDataSize)
            return false;
    } else {
        workingBuffer = (char*)payloadStart;
        workingSize = payloadLen;
        if (workingSize != expectedDataSize)
            return false;
    }
    (void)usedDecrypt;

    LockFrame();
    bool ok = true;
    if (isFullFrame) {
        memcpy(img->pData, workingBuffer, img->dataSize);
        img->isFullFrame = true;
        lastDirty.SetFull(img->width, img->height);
        lastChanged = true;
    } else {
        if (isTrans) {
            ok = BmpStream::ApplyTranslationResidual(img, transDx, transDy,
                    workingBuffer, img->stride, img->width, img->height);
            if (ok)
                lastDirty.SetFull(img->width, img->height);
        } else {
            FrameDirty xorDirty;
            ok = BmpStream::ApplyDiff(img->pData, img->stride, img->bitsPerPixel,
                    workingBuffer, img->stride, img->width, img->height, 0, 0, false, &xorDirty);
            if (ok)
                lastDirty = xorDirty;
        }
        if (ok) {
            img->isFullFrame = false;
            lastChanged = lastDirty.any;
        }
    }
    UnlockFrame();
    return ok;
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
bool RemoteClient::FetchBitmap(HBITMAP& result, bool* frameChanged) {
    if (frameChanged)
        *frameChanged = false;
    lastChanged = false;

    if (!FlushInput())
        return false;

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
    LockFrame();
    bool bufferOk = EnsureFrameBuffer(bufWidth, bufHeight, header.bitsPerPixel, bufStride);
    UnlockFrame();
    if (!bufferOk) {
        return false;
    }

    if (HandleNoChangeFrame(header, result)) {
        remoteScreenWidth = pFrameBuffer->width;
        remoteScreenHeight = pFrameBuffer->height;
        lastChanged = false;
        if (frameChanged)
            *frameChanged = false;
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
    if (frameChanged)
        *frameChanged = lastChanged;
    return true;
}

// Bitmapa jest sekcja DIB wypelniana w ReceiveImageData. Tu tylko sprawdzamy, ze zyje.
bool RemoteClient::CreateOrUpdateBitmap(const ImageData* img) {
    if (!img || !img->pData || img->width <= 0 || img->height <= 0)
        return false;
    return (hBitmap != NULL && bitmapWidth == img->width && bitmapHeight == img->height);
}
