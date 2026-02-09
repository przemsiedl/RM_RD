#include "RemoteServer.h"
#include "InputExecutor.h"
#include <stdio.h>

// ===== KONSTRUKTOR =====
RemoteServer::  RemoteServer(int _port) {
    port = _port;
    listenSocket = INVALID_SOCKET;
    running = false;
    listenThread = NULL;
    listenThreadId = 0;
    
    // Utworz FrameProvider (24 bpp dla wydajnosci)
    frameProvider = new FrameProvider(24);
    
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
    if (frameProvider) {
        delete frameProvider;
        frameProvider = NULL;
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
    
    if (server->frameProvider) {
        server->frameProvider->Reset();
    }
    
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
    
    if (!frameProvider || !frameProvider->GetFrame(frame)) {
        return false;
    }
    
    bool result = SendFrame(socket, frame);
    
    return result;
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
    if (frameProvider) {
        frameProvider->SetFrameGenerator(callback, userData);
    }
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
    
    return InputExecutor::ExecuteMouseCommand(cmd, data);
}

// ===== OBSŁUGA KOMEND KLAWIATURY =====
bool RemoteServer::ProcessKeyCommand(SOCKET socket, const FrameCmd& cmd) {
    DataKey data;
    
    if (! ReceiveKeyData(socket, data)) {
        return false;
    }
    
    return InputExecutor::ExecuteKeyCommand(cmd, data);
}