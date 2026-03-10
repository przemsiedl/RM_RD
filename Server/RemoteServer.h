#ifndef RemoteServerH
#define RemoteServerH

#include <windows.h>
#include <winsock.h>
#include "../Shared/Frame.h"
#include "../Shared/BmpStream.h"
#include "FrameProvider.h"

#define MAX_CLIENTS 10

#define MIN_FRAME_INTERVAL_MS 45

struct ClientConnection {
    SOCKET socket;
    SOCKADDR_IN address;
    HANDLE thread;
    DWORD threadId;
    bool active;
    void* userData;
    ImageData* pLastSentFrame;
    DWORD lastFrameSendTime;

    ClientConnection() {
        socket = INVALID_SOCKET;
        thread = NULL;
        threadId = 0;
        active = false;
        userData = NULL;
        pLastSentFrame = NULL;
        lastFrameSendTime = 0;
        memset(&address, 0, sizeof(address));
    }
};

class RemoteServer {
private:  
    int port;
    SOCKET listenSocket;
    bool running;
    HANDLE listenThread;
    DWORD listenThreadId;
    
    // Provider ramek
    FrameProvider* frameProvider;
    
    // Lista klientow
    ClientConnection clients[MAX_CLIENTS];
    CRITICAL_SECTION clientsSection;
    
    // Metody prywatne - inicjalizacja
    bool InitializeSocket();
    void CleanupSocket();
    
    // Watki
    static DWORD WINAPI ListenThreadProc(LPVOID param);
    static DWORD WINAPI ClientThreadProc(LPVOID param);
    
    void ListenLoop();
    void HandleClient(ClientConnection* client);
    
    // Zarzadzanie klientami
    bool AcceptClient(ClientConnection* client);
    void DisconnectClient(ClientConnection* client);
    void DisconnectAllClients();
    
    int FindFreeClientSlot();
    int GetActiveClientCount();
    
    // Protokol - odbieranie/wysylanie
    bool ReceiveCommand(SOCKET socket, FrameCmd& cmd);
    bool ReceiveMouseData(SOCKET socket, DataMouse& data);
    bool ReceiveKeyData(SOCKET socket, DataKey& data);
    bool SendFrame(SOCKET socket, FrameBmp& frame);
    bool SendHeader(SOCKET socket, HeaderBmp& header);
    bool SendData(SOCKET socket, const char* data, int size);
    
    // Obsluga komend
    bool ProcessCommand(ClientConnection* client, FrameCmd& cmd);
    bool HandleGetFrame(ClientConnection* client);
    bool ProcessMouseCommand(ClientConnection* client, const FrameCmd& cmd);
    bool ProcessKeyCommand(ClientConnection* client, const FrameCmd& cmd);
    
public:
    RemoteServer(int _port = 8080);
    ~RemoteServer();
    
    // Uruchomienie/zatrzymanie serwera
    bool Start();
    void Stop();
    bool IsRunning() const;
    
    // Ustawienie callbacka do generowania ramek
    void SetFrameGenerator(FrameGeneratorCallback callback, void* userData = NULL);
    
    // Informacje o serwerze
    int GetPort() const;
    int GetClientCount();
};

#endif