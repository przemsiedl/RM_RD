#include "RemoteServer.h"
#include <stdio.h>

// ===== KONSTRUKTOR =====
RemoteServer:: RemoteServer(int _port) {
    port = _port;
    listenSocket = INVALID_SOCKET;
    running = false;
    listenThread = NULL;
    listenThreadId = 0;
    frameCallback = NULL;
    callbackUserData = NULL;
    
    // Inicjalizacja sekcji krytycznej
    InitializeCriticalSection(&clientsSection);
    
    // Inicjalizacja WinSock
    WSADATA wsaData;
    WSAStartup(MAKEWORD(1, 1), &wsaData);
}

// ===== DESTRUKTOR =====
RemoteServer::~RemoteServer() {
    Stop();
    DeleteCriticalSection(&clientsSection);
    WSACleanup();
}

// ===== INICJALIZACJA SOCKETU =====
bool RemoteServer::InitializeSocket() {
    // Utw�rz socket nas�uchuj�cy
    listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        return false;
    }
    
    // Pozw�l na ponowne u�ycie adresu
    int reuse = 1;
    setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));
    
    // Przygotuj adres serwera
    SOCKADDR_IN serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    serverAddr.sin_addr.s_addr = INADDR_ANY;  // Wszystkie interfejsy
    
    // Binduj socket do adresu
    if (bind(listenSocket, (PSOCKADDR)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        closesocket(listenSocket);
        listenSocket = INVALID_SOCKET;
        return false;
    }
    
    // Rozpocznij nas�uchiwanie
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
        return true;  // Ju� dzia�a
    }
    
    // Inicjalizuj socket
    if (!InitializeSocket()) {
        return false;
    }
    
    // Utw�rz w�tek nas�uchuj�cy
    running = true;
    listenThread = CreateThread(NULL, 0, ListenThreadProc, this, 0, &listenThreadId);
    
    if (! listenThread) {
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
    
    // Zamknij socket nas�uchuj�cy (to przerwie accept())
    CleanupSocket();
    
    // Roz��cz wszystkich klient�w
    DisconnectAllClients();
    
    // Poczekaj na zako�czenie w�tku nas�uchuj�cego
    if (listenThread) {
        WaitForSingleObject(listenThread, 5000);
        CloseHandle(listenThread);
        listenThread = NULL;
    }
}

// ===== W�TEK NAS�UCHUJ�CY =====
DWORD WINAPI RemoteServer::ListenThreadProc(LPVOID param) {
    RemoteServer* server = (RemoteServer*)param;
    server->ListenLoop();
    return 0;
}

// ===== P�TLA NAS�UCHIWANIA =====
void RemoteServer::ListenLoop() {
    while (running) {
        // Znajd� wolne miejsce dla klienta
        int clientIndex = FindFreeClientSlot();
        if (clientIndex < 0) {
            Sleep(100);  // Brak miejsca - czekaj
            continue;
        }
        
        ClientConnection* client = &clients[clientIndex];
        
        // Akceptuj nowe po��czenie
        if (AcceptClient(client)) {
            // Utw�rz w�tek dla klienta
            client->thread = CreateThread(NULL, 0, ClientThreadProc, client, 0, &client->threadId);
            
            if (!client->thread) {
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
    
    // Ustaw timeouty
    int timeout = 30000;  // 30 sekund
    setsockopt(client->socket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
    setsockopt(client->socket, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));
    
    // Wy��cz algorytm Nagle
    int flag = 1;
    setsockopt(client->socket, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(flag));
    
    EnterCriticalSection(&clientsSection);
    client->active = true;
    LeaveCriticalSection(&clientsSection);
    
    return true;
}

// ===== W�TEK OBS�UGI KLIENTA =====
DWORD WINAPI RemoteServer::ClientThreadProc(LPVOID param) {
    ClientConnection* client = (ClientConnection*)param;
    RemoteServer* server = (RemoteServer*)client->userData;
    
    // Zapisz wska�nik do serwera
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (&server->clients[i] == client) {
            client->userData = server;
            break;
        }
    }
    
    server->HandleClient(client);
    return 0;
}

// ===== OBS�UGA KLIENTA =====
void RemoteServer::HandleClient(ClientConnection* client) {
    while (running && client->active) {
        FrameCmd cmd;
        
        // Odbierz komend� od klienta
        if (! ReceiveCommand(client->socket, cmd)) {
            break;  // B��d lub roz��czenie
        }
        
        // Przetw�rz komend�
        if (!ProcessCommand(client->socket, cmd)) {
            break;  // B��d przetwarzania
        }
    }
    
    // Roz��cz klienta
    DisconnectClient(client);
}

// ===== ODBIERANIE KOMENDY =====
bool RemoteServer::ReceiveCommand(SOCKET socket, FrameCmd& cmd) {
    int received = recv(socket, (char*)&cmd, sizeof(FrameCmd), 0);
    
    if (received != sizeof(FrameCmd)) {
        return false;
    }
    
    // Weryfikuj magic
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
        case CMD_KEY_PRESS: 
            return ProcessKeyCommand(socket, cmd);
        
        default: 
            return false;  // Nieznana komenda
    }
}

// ===== OBS�UGA KOMENDY GET_FRAME =====
bool RemoteServer::HandleGetFrame(SOCKET socket) {
    Frame frame;
    
    // Je�li jest callback, u�yj go
    if (frameCallback) {
        if (! frameCallback(frame, callbackUserData)) {
            return false;
        }
    } else {
        // Domy�lnie - zr�b screenshot
        if (!CaptureScreen(frame)) {
            return false;
        }
    }
    
    // Wy�lij ramk� do klienta
    bool result = SendFrame(socket, frame);

    // Zwolnij pami��
    //if (frame.data) {
    //    delete[] frame.data;
    //    frame.data = NULL;
    //}

    return result;
}

// ===== CAPTURE EKRANU (zaktualizowany) =====
bool RemoteServer::CaptureScreen(Frame& frame) {
    HDC hdcScreen = GetDC(NULL);
    if (!hdcScreen) {
        return false;
    }
    
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, screenWidth, screenHeight);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);
    
    BitBlt(hdcMem, 0, 0, screenWidth, screenHeight, hdcScreen, 0, 0, SRCCOPY);
    
    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = screenWidth;
    bmi.bmiHeader.biHeight = -screenHeight;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 24;
    bmi.bmiHeader.biCompression = BI_RGB;
    
    int stride = ((screenWidth * 3 + 3) / 4) * 4;
    int dataSize = stride * screenHeight;
    
    // Alokuj przez DataBmp
    frame.data.data = new char[dataSize];
    if (!frame.data.data) {
        SelectObject(hdcMem, hOldBitmap);
        DeleteObject(hBitmap);
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);
        return false;
    }
    
    int result = GetDIBits(hdcScreen, hBitmap, 0, screenHeight, 
                          frame.data.data, &bmi, DIB_RGB_COLORS);
    
    frame.header.x = 0;
    frame.header.y = 0;
    frame.header.width = screenWidth;
    frame.header.height = screenHeight;
    frame.header.length = dataSize;
    
    SelectObject(hdcMem, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
    
    return (result != 0);
}

// ===== WYSYLANIE RAMKI (zaktualizowany) =====
bool RemoteServer::SendFrame(SOCKET socket, Frame& frame) {
    if (!SendHeader(socket, frame. header)) {
        return false;
    }
    
    if (frame.header.length > 0 && frame.data.data) {
        if (!SendData(socket, frame.data.data, frame.header.length)) {
            return false;
        }
    }
    
    return true;
}

// ===== WYSYLANIE NAGLOWKA (zaktualizowany) =====
bool RemoteServer::SendHeader(SOCKET socket, HeaderBmp& header) {
    int sent = send(socket, (char*)&header, sizeof(HeaderBmp), 0);
    return (sent == sizeof(HeaderBmp));
}

// ===== WYSY�ANIE DANYCH =====
bool RemoteServer:: SendData(SOCKET socket, const char* data, int size) {
    if (! data || size <= 0) {
        return true;
    }
    
    int totalSent = 0;
    
    while (totalSent < size) {
        int sent = send(socket, data + totalSent, size - totalSent, 0);
        
        if (sent <= 0) {
            return false;  // B��d
        }
        
        totalSent += sent;
    }
    
    return true;
}

// ===== ROZ��CZANIE KLIENTA =====
void RemoteServer::DisconnectClient(ClientConnection* client) {
    EnterCriticalSection(&clientsSection);
    
    if (client->active) {
        client->active = false;
        
        if (client->socket != INVALID_SOCKET) {
            closesocket(client->socket);
            client->socket = INVALID_SOCKET;
        }
        
        if (client->thread) {
            // Nie czekamy tutaj - w�tek zako�czy si� sam
            CloseHandle(client->thread);
            client->thread = NULL;
        }
    }
    
    LeaveCriticalSection(&clientsSection);
}

// ===== ROZ��CZANIE WSZYSTKICH KLIENT�W =====
void RemoteServer::DisconnectAllClients() {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active) {
            DisconnectClient(&clients[i]);
        }
    }
    
    // Poczekaj na zako�czenie w�tk�w
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].thread) {
            WaitForSingleObject(clients[i].thread, 2000);
        }
    }
}

// ===== ZNAJD� WOLNE MIEJSCE =====
int RemoteServer::FindFreeClientSlot() {
    EnterCriticalSection(&clientsSection);
    
    int index = -1;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i]. active) {
            // Dodatkowa inicjalizacja wska�nika do serwera
            clients[i]. userData = this;
            index = i;
            break;
        }
    }
    
    LeaveCriticalSection(&clientsSection);
    return index;
}

// ===== LICZBA AKTYWNYCH KLIENT�W =====
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
bool RemoteServer:: ReceiveKeyData(SOCKET socket, DataKey& data) {
    int received = recv(socket, (char*)&data, sizeof(DataKey), 0);
    return (received == sizeof(DataKey));
}

// ===== OBSLUGA KOMEND MYSZY (zaktualizowany) =====
bool RemoteServer::ProcessMouseCommand(SOCKET socket, const FrameCmd& cmd) {
    DataMouse data;
    
    if (!ReceiveMouseData(socket, data)) {
        return false;
    }
    
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    
    int absX = (data.x * 65535) / screenWidth;
    int absY = (data.y * 65535) / screenHeight;
    
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

// ===== OBSLUGA KOMEND KLAWIATURY (zaktualizowany) =====
bool RemoteServer::ProcessKeyCommand(SOCKET socket, const FrameCmd& cmd) {
    DataKey data;
    
    if (!ReceiveKeyData(socket, data)) {
        return false;
    }
    
    switch (cmd.cmd) {
        case CMD_KEY_DOWN:
            keybd_event(data.virtualKey, data.scanCode, 0, 0);
            break;
        
        case CMD_KEY_UP:
            keybd_event(data.virtualKey, data.scanCode, KEYEVENTF_KEYUP, 0);
            break;
        
        case CMD_KEY_PRESS: 
            keybd_event(data. virtualKey, data.scanCode, 0, 0);
            Sleep(50);
            keybd_event(data.virtualKey, data.scanCode, KEYEVENTF_KEYUP, 0);
            break;
    }
    
    return true;
}