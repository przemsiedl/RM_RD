#include "RemoteServer.h"
#include "../Shared/Compression.h"
#include <stdio.h>

// ===== KONSTRUKTOR =====
RemoteServer::  RemoteServer(int _port) {
    port = _port;
    listenSocket = INVALID_SOCKET;
    running = false;
    listenThread = NULL;
    listenThreadId = 0;
    frameCallback = NULL;
    callbackUserData = NULL;
    
    // Utworz BmpStream (24 bpp dla wydajnosci)
    pBmpStream = new BmpStream(24);
    
    // Inicjalizacja sekcji krytycznej
    InitializeCriticalSection(&clientsSection);
    
    // Inicjalizacja WinSock
    WSADATA wsaData;
    WSAStartup(MAKEWORD(1, 1), &wsaData);
}

// ===== DESTRUKTOR =====
RemoteServer:: ~RemoteServer() {
    Stop();
    
    // Zwolnij BmpStream
    if (pBmpStream) {
        delete pBmpStream;
        pBmpStream = NULL;
    }
    
    DeleteCriticalSection(&clientsSection);
    WSACleanup();
}

// ===== INICJALIZACJA SOCKETU =====
bool RemoteServer:: InitializeSocket() {
    listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        return false;
    }
    
    int reuse = 1;
    setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));
    
    SOCKADDR_IN serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.  sin_port = htons(port);
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(listenSocket, (PSOCKADDR)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        closesocket(listenSocket);
        listenSocket = INVALID_SOCKET;
        return false;
    }
    
    if (listen(listenSocket, MAX_CLIENTS) == SOCKET_ERROR) {
        closesocket(listenSocket);
        listenSocket = INVALID_SOCKET;
        return false;
    }
    
    return true;
}

// ===== CZYSZCZENIE SOCKETU =====
void RemoteServer::CleanupSocket() {
    if (listenSocket != INVALID_SOCKET) {
        closesocket(listenSocket);
        listenSocket = INVALID_SOCKET;
    }
}

// ===== URUCHOMIENIE SERWERA =====
bool RemoteServer::Start() {
    if (running) {
        return true;
    }
    
    if (!InitializeSocket()) {
        return false;
    }
    
    running = true;
    listenThread = CreateThread(NULL, 0, ListenThreadProc, this, 0, &listenThreadId);
    
    if (!listenThread) {
        running = false;
        CleanupSocket();
        return false;
    }
    
    return true;
}

// ===== ZATRZYMANIE SERWERA =====
void RemoteServer::Stop() {
    if (!running) {
        return;
    }
    
    running = false;
    CleanupSocket();
    DisconnectAllClients();
    
    if (listenThread) {
        WaitForSingleObject(listenThread, 5000);
        CloseHandle(listenThread);
        listenThread = NULL;
    }
}

// WĄTEK NASŁUCHUJĄCY =====
DWORD WINAPI RemoteServer::ListenThreadProc(LPVOID param) {
    RemoteServer* server = (RemoteServer*)param;
    server->ListenLoop();
    return 0;
}

// ===== PĘTLA NASŁUCHIWANIA =====
void RemoteServer::ListenLoop() {
    while (running) {
        int clientIndex = FindFreeClientSlot();
        if (clientIndex < 0) {
            Sleep(100);
            continue;
        }
        
        ClientConnection* client = &clients[clientIndex];
        
        if (AcceptClient(client)) {
            client->thread = CreateThread(NULL, 0, ClientThreadProc, client, 0, &client->threadId);
            
            if (!  client->thread) {
                DisconnectClient(client);
            }
        }
    }
}

// ===== AKCEPTOWANIE KLIENTA =====
bool RemoteServer::AcceptClient(ClientConnection* client) {
    int addrLen = sizeof(client->address);
    
    client->socket = accept(listenSocket, (PSOCKADDR)&client->address, &addrLen);
    
    if (client->socket == INVALID_SOCKET) {
        return false;
    }
    
    int timeout = 30000;
    setsockopt(client->socket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
    setsockopt(client->socket, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));
    
    int flag = 1;
    setsockopt(client->socket, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(flag));
    
    EnterCriticalSection(&clientsSection);
    client->active = true;
    LeaveCriticalSection(&clientsSection);
    
    return true;
}

// ===== WĄTEK OBSŁUGI KLIENTA =====
DWORD WINAPI RemoteServer::ClientThreadProc(LPVOID param) {
    ClientConnection* client = (ClientConnection*)param;
    RemoteServer* server = (RemoteServer*)client->userData;
    
    BmpStream* pBmpStream = server->pBmpStream;
    pBmpStream->Reset();
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (&server->clients[i] == client) {
            client->userData = server;
            break;
        }
    }
    
    server->HandleClient(client);
    return 0;
}

// ===== OBSŁUGA KLIENTA =====
void RemoteServer::HandleClient(ClientConnection* client) {
    while (running && client->active) {
        FrameCmd cmd;
        
        if (! ReceiveCommand(client->socket, cmd)) {
            break;
        }
        
        if (! ProcessCommand(client->socket, cmd)) {
            break;
        }
    }
    
    DisconnectClient(client);
}

// ===== ODBIERANIE KOMENDY =====
bool RemoteServer::ReceiveCommand(SOCKET socket, FrameCmd& cmd) {
    int received = recv(socket, (char*)&cmd, sizeof(FrameCmd), 0);
    
    if (received != sizeof(FrameCmd)) {
        return false;
    }
    
    if (cmd.magic[0] != 'C' || cmd.magic[1] != 'M' || cmd.magic[2] != 'D') {
        return false;
    }
    
    return true;
}

// ===== PRZETWARZANIE KOMENDY =====
bool RemoteServer::ProcessCommand(SOCKET socket, FrameCmd& cmd) {
    switch (cmd.cmd) {
        case CMD_GET_FRAME:
            return HandleGetFrame(socket);
        
        case CMD_MOUSE_MOVE:
        case CMD_MOUSE_LEFT_DOWN:
        case CMD_MOUSE_LEFT_UP:
        case CMD_MOUSE_RIGHT_DOWN:
        case CMD_MOUSE_RIGHT_UP:
        case CMD_MOUSE_MIDDLE_DOWN:
        case CMD_MOUSE_MIDDLE_UP:  
        case CMD_MOUSE_WHEEL:  
            return ProcessMouseCommand(socket, cmd);
        
        case CMD_KEY_DOWN:
        case CMD_KEY_UP:
            return ProcessKeyCommand(socket, cmd);
        
        default:  
            return false;
    }
}

// ===== OBSŁUGA KOMENDY GET_FRAME =====
bool RemoteServer::HandleGetFrame(SOCKET socket) {
    FrameBmp frame;
    
    if (frameCallback) {
        if (!frameCallback(frame, callbackUserData)) {
            return false;
        }
    } else {
        if (!CaptureScreen(frame)) {
            return false;
        }
    }
    
    bool result = SendFrame(socket, frame);
    
    return result;
}

// ===== KONWERSJA ImageData -> FrameBmp (POPRAWIONE) =====
void RemoteServer::ImageDataToFrameBmp(const ImageData* img, FrameBmp& frame) {
    frame.header.x = 0;
    frame.header. y = 0;
    frame.header.width = img->width;
    frame.header.height = img->height;
    frame.header.bitsPerPixel = img->bitsPerPixel;
    frame. header.stride = img->stride;
    
    // ===== UŻYJ isFullFrame Z ImageData =====
    if (img->isFullFrame) {
        frame.header.flags = FRAME_FLAG_FULL;
    } else {
        frame.header. flags = FRAME_FLAG_DIFF;
    }

    // ===== PUSTA RÓŻNICA =====
    if (!img->isFullFrame && img->isEmptyDiff) {
        frame.header.flags |= FRAME_FLAG_NOCHANGE;
        frame.header.length = 0;
        frame.data.Reset();
        return;
    }
    
    // ===== KOMPRESJA DANYCH =====
    // Alokuj bufor na skompresowane dane (maksymalnie taki sam rozmiar + zapas)
    int maxCompressedSize = img->dataSize + 1024;  // Zapas na header kompresji
    char* compressedData = new char[maxCompressedSize];
    int compressedSize = 0;
    
    Compression::encrypt(img->pData, img->dataSize, compressedData, compressedSize);
    
    // Ustaw dane i flagę kompresji
    frame.data. SetReferenceWithOwn(compressedData);
    frame.header.flags |= FRAME_FLAG_COMPRESSED;
    frame.header.length = compressedSize;
}

// ===== CAPTURE EKRANU (używa BmpStream) =====
bool RemoteServer::CaptureScreen(FrameBmp& frame) {
    if (!pBmpStream) {
        return false;
    }
    
    pBmpStream->Capture();
    pBmpStream->CalcDiff();
    
    const ImageData* diff = pBmpStream->GetDiff();
    if (!diff || !  diff->pData) {
        return false;
    }
    
    ImageDataToFrameBmp(diff, frame);
    
    return true;
}

// ===== WYSYŁANIE RAMKI =====
bool RemoteServer::SendFrame(SOCKET socket, FrameBmp& frame) {
    if (!SendHeader(socket, frame.header)) {
        return false;
    }
    
    if (frame.header.length > 0 && frame.data.data) {
        if (!SendData(socket, frame.data. data, frame.header.length)) {
            return false;
        }
    }
    
    return true;
}

// ===== WYSYŁANIE NAGŁÓWKA =====
bool RemoteServer::SendHeader(SOCKET socket, HeaderBmp& header) {
    int sent = send(socket, (char*)&header, sizeof(HeaderBmp), 0);
    return (sent == sizeof(HeaderBmp));
}

// ===== WYSYŁANIE DANYCH =====
bool RemoteServer::SendData(SOCKET socket, const char* data, int size) {
    if (!  data || size <= 0) {
        return true;
    }
    
    int totalSent = 0;
    
    while (totalSent < size) {
        int sent = send(socket, data + totalSent, size - totalSent, 0);
        
        if (sent <= 0) {
            return false;
        }
        
        totalSent += sent;
    }
    
    return true;
}

// ===== ROZŁĄCZANIE KLIENTA =====
void RemoteServer::DisconnectClient(ClientConnection* client) {
    EnterCriticalSection(&clientsSection);
    
    if (client->active) {
        client->active = false;
        
        if (client->socket != INVALID_SOCKET) {
            closesocket(client->socket);
            client->socket = INVALID_SOCKET;
        }
        
        if (client->thread) {
            CloseHandle(client->thread);
            client->thread = NULL;
        }
    }
    
    LeaveCriticalSection(&clientsSection);
}

// ===== ROZŁĄCZANIE WSZYSTKICH KLIENTÓW =====
void RemoteServer::DisconnectAllClients() {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active) {
            DisconnectClient(&clients[i]);
        }
    }
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].thread) {
            WaitForSingleObject(clients[i].thread, 2000);
        }
    }
}

// ===== ZNAJDŹ WOLNE MIEJSCE =====
int RemoteServer::FindFreeClientSlot() {
    EnterCriticalSection(&clientsSection);
    
    int index = -1;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].  active) {
            clients[i].  userData = this;
            index = i;
            break;
        }
    }
    
    LeaveCriticalSection(&clientsSection);
    return index;
}

// ===== LICZBA AKTYWNYCH KLIENTÓW =====
int RemoteServer::GetActiveClientCount() {
    EnterCriticalSection(&clientsSection);
    
    int count = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active) {
            count++;
        }
    }
    
    LeaveCriticalSection(&clientsSection);
    return count;
}

// ===== GETTERY =====
bool RemoteServer::IsRunning() const {
    return running;
}

int RemoteServer::GetPort() const {
    return port;
}

int RemoteServer::GetClientCount() {
    return GetActiveClientCount();
}

// ===== USTAWIENIE CALLBACKA =====
void RemoteServer::SetFrameGenerator(FrameGeneratorCallback callback, void* userData) {
    frameCallback = callback;
    callbackUserData = userData;
}

// ===== ODBIERANIE DANYCH MYSZY =====
bool RemoteServer::ReceiveMouseData(SOCKET socket, DataMouse& data) {
    int received = recv(socket, (char*)&data, sizeof(DataMouse), 0);
    return (received == sizeof(DataMouse));
}

// ===== ODBIERANIE DANYCH KLAWIATURY =====
bool RemoteServer::ReceiveKeyData(SOCKET socket, DataKey& data) {
    int received = recv(socket, (char*)&data, sizeof(DataKey), 0);
    return (received == sizeof(DataKey));
}

// ===== OBSŁUGA KOMEND MYSZY =====
bool RemoteServer::ProcessMouseCommand(SOCKET socket, const FrameCmd& cmd) {
    DataMouse data;
    
    if (!ReceiveMouseData(socket, data)) {
        return false;
    }
    
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    
    int absX = (data.x * 65535) / screenWidth;
    int absY = (data.  y * 65535) / screenHeight;
    
    DWORD flags = MOUSEEVENTF_ABSOLUTE;
    
    switch (cmd.cmd) {
        case CMD_MOUSE_MOVE:
            flags |= MOUSEEVENTF_MOVE;
            mouse_event(flags, absX, absY, 0, 0);
            break;
        
        case CMD_MOUSE_LEFT_DOWN:
            flags |= MOUSEEVENTF_MOVE | MOUSEEVENTF_LEFTDOWN;
            mouse_event(flags, absX, absY, 0, 0);
            break;
        
        case CMD_MOUSE_LEFT_UP: 
            flags |= MOUSEEVENTF_MOVE | MOUSEEVENTF_LEFTUP;
            mouse_event(flags, absX, absY, 0, 0);
            break;
        
        case CMD_MOUSE_RIGHT_DOWN:  
            flags |= MOUSEEVENTF_MOVE | MOUSEEVENTF_RIGHTDOWN;
            mouse_event(flags, absX, absY, 0, 0);
            break;
        
        case CMD_MOUSE_RIGHT_UP: 
            flags |= MOUSEEVENTF_MOVE | MOUSEEVENTF_RIGHTUP;
            mouse_event(flags, absX, absY, 0, 0);
            break;
        
        case CMD_MOUSE_MIDDLE_DOWN:
            flags |= MOUSEEVENTF_MOVE | MOUSEEVENTF_MIDDLEDOWN;
            mouse_event(flags, absX, absY, 0, 0);
            break;
        
        case CMD_MOUSE_MIDDLE_UP:
            flags |= MOUSEEVENTF_MOVE | MOUSEEVENTF_MIDDLEUP;
            mouse_event(flags, absX, absY, 0, 0);
            break;
        
        case CMD_MOUSE_WHEEL:
            flags |= MOUSEEVENTF_WHEEL;
            mouse_event(flags, absX, absY, data.wheelDelta, 0);
            break;
    }
    
    return true;
}

// ===== OBSŁUGA KOMEND KLAWIATURY =====
bool RemoteServer::ProcessKeyCommand(SOCKET socket, const FrameCmd& cmd) {
    DataKey data;
    
    if (! ReceiveKeyData(socket, data)) {
        return false;
    }
    
    switch (cmd.cmd) {
        case CMD_KEY_DOWN:
            keybd_event(data.virtualKey, data.scanCode, 0, 0);
            break;
        
        case CMD_KEY_UP:
            keybd_event(data.  virtualKey, data.scanCode, KEYEVENTF_KEYUP, 0);
            break;
    }
    
    return true;
}