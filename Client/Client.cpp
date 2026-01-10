#include <windows.h>
#include <stdio.h>
#include "..\Shared\Frame.h"
#include "RemoteClient.h"

RemoteClient* g_client = NULL;
HBITMAP g_hBitmap = NULL;
DWORD g_lastFrameTime = 0;
DWORD g_frameCount = 0;

// Konwersja wspolrzednych lokalnych na zdalne
void LocalToRemoteCoords(HWND hwnd, int localX, int localY, int& remoteX, int& remoteY) {
    if (g_client->remoteScreenWidth == 0 || g_client->remoteScreenHeight == 0) {
        remoteX = localX;
        remoteY = localY;
        return;
    }

    RECT clientRect;
    GetClientRect(hwnd, &clientRect);

    int clientWidth = clientRect.right - clientRect.left;
    int clientHeight = clientRect.bottom - clientRect.top;

    // Przeskaluj wspolrzedne
    remoteX = (localX * g_client->remoteScreenWidth) / clientWidth;
    remoteY = (localY * g_client->remoteScreenHeight) / clientHeight;
}

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
            g_client = new RemoteClient("192.168.1.10", 8080);
            g_client->Init();

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
            
            if (g_client) {
                delete g_client;
                g_client = NULL;
            }
            
            PostQuitMessage(0);
            break;
        }
        
        case WM_LBUTTONDOWN: {
            if (! g_client) break;
            
            int localX = LOWORD(lParam);
            int localY = HIWORD(lParam);
            int remoteX, remoteY;
            
            LocalToRemoteCoords(hwnd, localX, localY, remoteX, remoteY);
            g_client->MouseLeftDown(remoteX, remoteY);
            break;
        }
        
        case WM_LBUTTONUP: {
            if (!g_client) break;
            
            int localX = LOWORD(lParam);
            int localY = HIWORD(lParam);
            int remoteX, remoteY;
            
            LocalToRemoteCoords(hwnd, localX, localY, remoteX, remoteY);
            g_client->MouseLeftUp(remoteX, remoteY);
            break;
        }
        
        case WM_RBUTTONDOWN:  {
            if (!g_client) break;
            
            int localX = LOWORD(lParam);
            int localY = HIWORD(lParam);
            int remoteX, remoteY;
            
            LocalToRemoteCoords(hwnd, localX, localY, remoteX, remoteY);
            g_client->MouseRightDown(remoteX, remoteY);
            break;
        }
        
        case WM_RBUTTONUP: {
            if (!g_client) break;
            
            int localX = LOWORD(lParam);
            int localY = HIWORD(lParam);
            int remoteX, remoteY;
            
            LocalToRemoteCoords(hwnd, localX, localY, remoteX, remoteY);
            g_client->MouseRightUp(remoteX, remoteY);
            break;
        }
        
        case WM_MBUTTONDOWN: {
            if (! g_client) break;
            
            int localX = LOWORD(lParam);
            int localY = HIWORD(lParam);
            int remoteX, remoteY;
            
            LocalToRemoteCoords(hwnd, localX, localY, remoteX, remoteY);
            g_client->MouseMiddleDown(remoteX, remoteY);
            break;
        }
        
        case WM_MBUTTONUP:  {
            if (!g_client) break;
            
            int localX = LOWORD(lParam);
            int localY = HIWORD(lParam);
            int remoteX, remoteY;
            
            LocalToRemoteCoords(hwnd, localX, localY, remoteX, remoteY);
            g_client->MouseMiddleUp(remoteX, remoteY);
            break;
        }
        
        case WM_MOUSEMOVE: {
            if (! g_client) break;
            
            int localX = LOWORD(lParam);
            int localY = HIWORD(lParam);
            int remoteX, remoteY;
            
            LocalToRemoteCoords(hwnd, localX, localY, remoteX, remoteY);
            g_client->MouseMove(remoteX, remoteY);
            break;
        }
              /*
        case WM_MOUSEWHEEL: {
            if (! g_client) break;

            int delta = (short)HIWORD(wParam);  // Poprawione dla NT4. 0
            int localX = LOWORD(lParam);
            int localY = HIWORD(lParam);
            int remoteX, remoteY;

            // Przelicz wspolrzedne ekranowe na klienckie
            POINT pt;
            pt.x = localX;
            pt.y = localY;
            ScreenToClient(hwnd, &pt);

            LocalToRemoteCoords(hwnd, pt.x, pt.y, remoteX, remoteY);
            g_client->MouseWheel(remoteX, remoteY, delta);
            break;
        }   */

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
            
            g_client->KeyDown((WORD)wParam);
            break;
        }
        
        case WM_SYSKEYUP:    // Dla Alt + klawisz
        case WM_KEYUP: {
            if (! g_client) break;
            g_client->KeyUp((WORD)wParam);
            break;
        }
        
        default: 
            return DefWindowProc(hwnd, message, wParam, lParam);
    }
    
    return 0;
}

// Glowna funkcja
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrevInst,
                   LPSTR args, int nShowCmd) {
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