#ifndef RemoteClientH
#define RemoteClientH

#include <windows.h>
#include "..\Shared\Frame.h"
#include "..\Shared\BmpStream.h"

class RemoteClient {
private:    
    char* host;
    int port;
    SOCKET socket;
    
    // Bufor roboczy - jeden ImageData na cały czas życia
    ImageData* pFrameBuffer;
    
    HBITMAP hBitmap;

    bool Connect();
    void Disconnect();
    
    bool SendCommand(const FrameCmd& cmd, const void* data = NULL, int dataSize = 0);
    bool ReceiveFrameHeader(HeaderBmp& header);
    bool ValidateFrameHeader(const HeaderBmp& header) const;
    bool HandleNoChangeFrame(const HeaderBmp& header, HBITMAP& result);
    bool ReceiveImageData(ImageData* img, const HeaderBmp& header);
    
    HBITMAP CreateBitmapFromImageData(const ImageData* img);
    
    // Inicjalizacja/realokacja bufora
    bool EnsureFrameBuffer(int width, int height, int bpp, int stride);

public:
    RemoteClient(const char* _host, int port = 8080);
    ~RemoteClient();

    int remoteScreenWidth;
    int remoteScreenHeight;

    bool IsConnected() const;
    bool FetchBitmap(HBITMAP& result);
    
    // ===== KOMENDY MYSZY =====
    bool MouseMove(int x, int y);
    bool MouseLeftDown(int x, int y);
    bool MouseLeftUp(int x, int y);
    bool MouseRightDown(int x, int y);
    bool MouseRightUp(int x, int y);
    bool MouseMiddleDown(int x, int y);
    bool MouseMiddleUp(int x, int y);
    bool MouseWheel(int x, int y, int delta);
    
    // ===== KOMENDY KLAWIATURY =====
    bool KeyDown(WORD virtualKey);
    bool KeyUp(WORD virtualKey);
};

#endif