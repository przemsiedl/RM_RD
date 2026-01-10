#ifndef RemoteClientH
#define RemoteClientH

#include <windows.h>
#include "..\Frame.h"

class RemoteClient {
private:  
    char* host;
    int port;
    SOCKET socket;
    HBITMAP hBitmap;

    bool Connect();
    void Disconnect();
    
    bool SendCommand(const FrameCmd& cmd, const void* data = NULL, int dataSize = 0);
    bool ReceiveHeader(HeaderBmp& header);
    bool ReceiveData(char* buffer, int size);
    
    bool Get(FrameBmp& frame);
    
    HBITMAP CreateBitmapFromFrame(FrameBmp& frame);

public:
    RemoteClient(const char* _host, int port = 8080);
    ~RemoteClient();

    int remoteScreenWidth;
    int remoteScreenHeight;

    void Init();
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