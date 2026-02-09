#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "..\Shared\Frame.h"
#include "RemoteClient.h"
#include "InputSender.h"
#include "ClientInputHandler.h"
#include "ClientArgs.h"

RemoteClient* g_client = NULL;
InputSender* g_inputSender = NULL;
ClientInputHandler* g_inputHandler = NULL;
HBITMAP g_hBitmap = NULL;
DWORD g_lastFrameTime = 0;
DWORD g_frameCount = 0;

// Rysowanie tekstu z informacjami
void DrawStatusText(HDC hdc, HWND hwnd) {
    RECT rect;
    GetClientRect(hwnd, &rect);
    
    char statusText[256];

    if (g_client->IsConnected()) {
        DWORD now = GetTickCount();
        DWORD elapsed = (now - g_lastFrameTime) / 1000;
        
        sprintf(statusText,
                "Polaczony | Ramek: %d | Ostatnia:  %d sek temu", 
                g_frameCount,
                elapsed);
        
        SetTextColor(hdc, RGB(0, 255, 0));
    } else {
        sprintf(statusText, "Rozlaczony - probuje polaczyc.. .");
        SetTextColor(hdc, RGB(255, 0, 0));
    }
    
    SetBkMode(hdc, TRANSPARENT);

    HFONT hFont = CreateFont(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                             DEFAULT_PITCH | FF_SWISS, "Arial");
    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

    TextOut(hdc, 10, 10, statusText, strlen(statusText));

    SelectObject(hdc, hOldFont);
    DeleteObject(hFont);
}

// Procedura okna
LRESULT CALLBACK WindowProcedure(HWND hwnd, UINT message,
                                  WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:  {
            g_client = new RemoteClient(g_serverHost, g_serverPort);
            g_inputSender = new InputSender(g_client);
            g_inputHandler = new ClientInputHandler(g_client, g_inputSender);

            HBITMAP hBitmap = NULL;
            if (g_client->FetchBitmap(hBitmap)) {
                g_hBitmap = hBitmap;
                g_frameCount++;
                g_lastFrameTime = GetTickCount();
            }
            break;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            if (g_hBitmap) {
                // Rysuj bitmape
                HDC hdcMem = CreateCompatibleDC(hdc);
                HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, g_hBitmap);
                
                BITMAP bm;
                GetObject(g_hBitmap, sizeof(bm), &bm);
                
                // Skaluj do rozmiaru okna
                RECT clientRect;
                GetClientRect(hwnd, &clientRect);
                
                SetStretchBltMode(hdc, COLORONCOLOR);
                StretchBlt(hdc, 0, 0, 
                          clientRect.right, clientRect.bottom,
                          hdcMem, 0, 0, 
                          bm.bmWidth, bm.bmHeight, 
                          SRCCOPY);
                
                SelectObject(hdcMem, hOldBitmap);
                DeleteDC(hdcMem);
            } else {
                // Brak bitmapy - rysuj tlo
                RECT rect;
                GetClientRect(hwnd, &rect);
                FillRect(hdc, &rect, (HBRUSH)GetStockObject(BLACK_BRUSH));
                
                SetTextColor(hdc, RGB(255, 255, 255));
                SetBkMode(hdc, TRANSPARENT);
                
                char* text = "Laczenie z serwerem...";
                DrawText(hdc, text, -1, &rect, 
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            
            // Rysuj status
            DrawStatusText(hdc, hwnd);
            
            EndPaint(hwnd, &ps);
            break;
        }
        
        case WM_TIMER: {
            if (wParam == 1000 && g_client != NULL) {
                HBITMAP hBitmap = NULL;
                if (g_client->FetchBitmap(hBitmap)) {
                    g_hBitmap = hBitmap;
                    g_frameCount++;
                    g_lastFrameTime = GetTickCount();
                    InvalidateRect(hwnd, NULL, TRUE);
                }
            }
            break;
        }
        
        case WM_DESTROY: {
            KillTimer(hwnd, 1000);
            
            if (g_hBitmap) {
                DeleteObject(g_hBitmap);
                g_hBitmap = NULL;
            }
            
            if (g_inputHandler) {
                delete g_inputHandler;
                g_inputHandler = NULL;
            }

            if (g_inputSender) {
                delete g_inputSender;
                g_inputSender = NULL;
            }

            if (g_client) {
                delete g_client;
                g_client = NULL;
            }
            
            PostQuitMessage(0);
            break;
        }
        
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MOUSEMOVE: {
            if (g_inputHandler) {
                g_inputHandler->HandleMessage(hwnd, message, wParam, lParam);
            }
            break;
        }
        case WM_SYSKEYDOWN:    // Dla Alt + klawisz
        case WM_KEYDOWN:  {
            if (!g_client) break;
            
            // F5 - wymuszenie odswiezenia
            if (wParam == VK_F5) {
                HBITMAP hBitmap = NULL;
                if (g_client->FetchBitmap(hBitmap)) {
                    g_hBitmap = hBitmap;
                    g_frameCount++;
                    g_lastFrameTime = GetTickCount();
                    InvalidateRect(hwnd, NULL, TRUE);
                }
                break;
            }
            
            // ESC - zamkniecie
            if (wParam == VK_ESCAPE) {
                PostMessage(hwnd, WM_CLOSE, 0, 0);
                break;
            }

            if (g_inputHandler) {
                g_inputHandler->HandleMessage(hwnd, message, wParam, lParam);
            }
            break;
        }
        
        case WM_SYSKEYUP:    // Dla Alt + klawisz
        case WM_KEYUP: {
            if (g_inputHandler) {
                g_inputHandler->HandleMessage(hwnd, message, wParam, lParam);
            }
            break;
        }
        
        default: 
            return DefWindowProc(hwnd, message, wParam, lParam);
    }
    
    return 0;
}

// Glowna funkcja
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrevInst,
                   LPSTR lpCmdLine, int nShowCmd) {
    if (!ParseCommandLine(lpCmdLine)) {
        MessageBox(NULL,
            "Uzycie: Client.exe <host> [port]\n\n"
            "Np. Client.exe 192.168.1.10 8080",
            "Remote Screen Viewer", MB_ICONINFORMATION);
        return 0;
    }

    char* klasaOkna = "RemoteViewerClass";
    
    WNDCLASS wc = {0};
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hInstance = hInst;
    wc.lpszClassName = klasaOkna;
    wc.lpfnWndProc = WindowProcedure;
    
    if (! RegisterClass(&wc)) {
        MessageBox(NULL, "Blad rejestracji klasy!", "Blad", MB_ICONERROR);
        return -1;
    }
    
    // Pobierz rozmiar ekranu
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    
    // Utworz okno 80% rozmiaru ekranu, wysrodkowane
    int windowWidth = (screenWidth * 80) / 100;
    int windowHeight = (screenHeight * 80) / 100;
    int windowX = (screenWidth - windowWidth) / 2;
    int windowY = (screenHeight - windowHeight) / 2;
    
    HWND hwnd = CreateWindow(
        klasaOkna,
        "Remote Screen Viewer - ESC aby zamknac, F5 aby odswiezyc",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        windowX, windowY,
        windowWidth, windowHeight,
        NULL, NULL, hInst, NULL
    );
    
    if (! hwnd) {
        MessageBox(NULL, "Blad tworzenia okna!", "Blad", MB_ICONERROR);
        return -1;
    }
    
    // Timer:  50ms = 20 FPS
    SetTimer(hwnd, 1000, 50, NULL);
    
    MSG msg = {0};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    return 0;
}