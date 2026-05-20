#ifndef RemoteClientH
#define RemoteClientH

#include <windows.h>
#include "..\Shared\Frame.h"
#include "..\Shared\BmpStream.h"

class InputSender;

class RemoteClient {
private:    
    char* host;
    int port;
    SOCKET socket;
    
    // Bufor roboczy - jeden ImageData na cały czas życia
    ImageData* pFrameBuffer;
    
    HBITMAP hBitmap;
    int bitmapWidth, bitmapHeight;

    bool Connect();
    void Disconnect();
    
    bool SendCommand(const FrameCmd& cmd, const void* data = NULL, int dataSize = 0);
    bool ReceiveFrameHeader(HeaderBmp& header);
    bool ValidateFrameHeader(const HeaderBmp& header) const;
    bool HandleNoChangeFrame(const HeaderBmp& header, HBITMAP& result);
    bool ReceiveImageData(ImageData* img, const HeaderBmp& header);
    
    bool CreateOrUpdateBitmap(const ImageData* img);
    
    // Inicjalizacja/realokacja bufora
    bool EnsureFrameBuffer(int width, int height, int bpp, int stride);

    int diffBlockW;
    int diffBlockH;
    bool diffGridDirty;

public:
    RemoteClient(const char* _host, int port = 8080);
    ~RemoteClient();

    friend class InputSender;

    int remoteScreenWidth;
    int remoteScreenHeight;

    bool IsConnected() const;
    bool FetchBitmap(HBITMAP& result);

    // Siatka bloczkow XOR wysylana do serwera (CMD_SET_DIFF_GRID) przed kolejnymi GET_FRAME.
    // Wymiary 8..128 pikseli (serwer obcina). Domyslnie 32x32.
    void SetDiffGrid(int blockW, int blockH);
};

#endif