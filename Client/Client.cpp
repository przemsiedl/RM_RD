#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "../Shared/Frame.h"
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
volatile long g_netRun = 0;
volatile long g_forceFrame = 0;
HANDLE g_netThread = NULL;
HFONT g_statusFont = NULL;

static UINT NextFrameGap(DWORD fetchMs, bool changed) {
    if (!changed) {
        UINT gap = 200;
        if (fetchMs > 200)
            gap = fetchMs + 40;
        if (gap > 800)
            gap = 800;
        return gap;
    }
    UINT gap = fetchMs / 2;
    if (gap < 30)
        gap = 30;
    if (gap > 250)
        gap = 250;
    return gap;
}

static RECT MapRemoteToClient(HWND hwnd, int x, int y, int w, int h) {
    RECT cr;
    GetClientRect(hwnd, &cr);
    RECT r = cr;
    int rw = g_client ? g_client->remoteScreenWidth : 0;
    int rh = g_client ? g_client->remoteScreenHeight : 0;
    if (rw <= 0 || rh <= 0 || cr.right <= 0 || cr.bottom <= 0)
        return r;
    r.left = x * cr.right / rw;
    r.top = y * cr.bottom / rh;
    r.right = (x + w) * cr.right / rw + 1;
    r.bottom = (y + h) * cr.bottom / rh + 1;
    if (r.left < 0) r.left = 0;
    if (r.top < 0) r.top = 0;
    if (r.right > cr.right) r.right = cr.right;
    if (r.bottom > cr.bottom) r.bottom = cr.bottom;
    return r;
}

static void InvalidateFrame(HWND hwnd, bool changed) {
    RECT status;
    status.left = 0;
    status.top = 0;
    status.right = 480;
    status.bottom = 24;
    InvalidateRect(hwnd, &status, FALSE);
    if (!changed || !g_client)
        return;
    int x, y, w, h;
    g_client->LockFrame();
    bool has = g_client->GetLastDirty(x, y, w, h);
    int rw = g_client->remoteScreenWidth;
    int rh = g_client->remoteScreenHeight;
    g_client->UnlockFrame();
    if (!has || (w >= rw && h >= rh)) {
        InvalidateRect(hwnd, NULL, FALSE);
        return;
    }
    RECT r = MapRemoteToClient(hwnd, x, y, w, h);
    InvalidateRect(hwnd, &r, FALSE);
}

static DWORD WINAPI NetThreadProc(LPVOID param) {
    HWND hwnd = (HWND)param;
    DWORD nextFrame = GetTickCount();
    while (g_netRun && g_client) {
        if (g_client->HasPendingInput()) {
            if (!g_client->FlushInput())
                break;
        }

        DWORD now = GetTickCount();
        long force = g_forceFrame;
        if (force)
            g_forceFrame = 0;
        if (force || (long)(now - nextFrame) >= 0) {
            DWORD t0 = GetTickCount();
            HBITMAP hBitmap = NULL;
            bool changed = false;
            bool ok = g_client->FetchBitmap(hBitmap, &changed);
            DWORD dt = GetTickCount() - t0;
            if (!g_netRun)
                break;
            if (ok) {
                g_client->LockFrame();
                g_hBitmap = hBitmap;
                g_client->UnlockFrame();
                g_frameCount++;
                g_lastFrameTime = GetTickCount();
                if (hwnd)
                    InvalidateFrame(hwnd, changed);
            }
            UINT gap = NextFrameGap(dt, ok && changed);
            nextFrame = GetTickCount() + gap;
            if (!ok && g_netRun) {
                /* Po bledzie sieci odczekaj, zanim znowu polaczymy. */
                nextFrame = GetTickCount() + 400;
            }
        }

        HANDLE wake = g_client->GetInputEvent();
        if (wake)
            WaitForSingleObject(wake, 15);
        else
            Sleep(15);
    }
    return 0;
}

// Rysowanie tekstu z informacjami
void DrawStatusText(HDC hdc, HWND hwnd) {
    char statusText[256];

    if (g_client->IsConnected()) {
        DWORD now = GetTickCount();
        DWORD elapsed = (now - g_lastFrameTime) / 1000;

        sprintf(statusText,
                "Polaczony | Ramek: %lu | Ostatnia:  %lu sek temu",
                (unsigned long)g_frameCount,
                (unsigned long)elapsed);

        SetTextColor(hdc, RGB(0, 255, 0));
    } else {
        sprintf(statusText, "Rozlaczony - probuje polaczyc.. .");
        SetTextColor(hdc, RGB(255, 0, 0));
    }

    SetBkMode(hdc, TRANSPARENT);

    HFONT hOldFont = NULL;
    if (g_statusFont)
        hOldFont = (HFONT)SelectObject(hdc, g_statusFont);

    TextOut(hdc, 10, 10, statusText, strlen(statusText));

    if (g_statusFont)
        SelectObject(hdc, hOldFont);
}

static void PaintBitmap(HDC hdc, HWND hwnd, const RECT& paint) {
    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    int cw = clientRect.right - clientRect.left;
    int ch = clientRect.bottom - clientRect.top;
    if (cw <= 0 || ch <= 0)
        return;

    g_client->LockFrame();
    HBITMAP hb = g_hBitmap;
    if (!hb) {
        g_client->UnlockFrame();
        return;
    }

    BITMAP bm;
    GetObject(hb, sizeof(bm), &bm);

    HDC hdcMem = CreateCompatibleDC(hdc);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, hb);

    RECT pr = paint;
    if (pr.left < 0) pr.left = 0;
    if (pr.top < 0) pr.top = 0;
    if (pr.right > cw) pr.right = cw;
    if (pr.bottom > ch) pr.bottom = ch;

    int srcL = pr.left * bm.bmWidth / cw;
    int srcT = pr.top * bm.bmHeight / ch;
    int srcR = (pr.right * bm.bmWidth + cw - 1) / cw;
    int srcB = (pr.bottom * bm.bmHeight + ch - 1) / ch;
    if (srcL < 0) srcL = 0;
    if (srcT < 0) srcT = 0;
    if (srcR > bm.bmWidth) srcR = bm.bmWidth;
    if (srcB > bm.bmHeight) srcB = bm.bmHeight;

    SetStretchBltMode(hdc, COLORONCOLOR);
    if (srcR > srcL && srcB > srcT && pr.right > pr.left && pr.bottom > pr.top) {
        StretchBlt(hdc, pr.left, pr.top, pr.right - pr.left, pr.bottom - pr.top,
                   hdcMem, srcL, srcT, srcR - srcL, srcB - srcT, SRCCOPY);
    }

    SelectObject(hdcMem, hOldBitmap);
    DeleteDC(hdcMem);
    g_client->UnlockFrame();
}

// Procedura okna
LRESULT CALLBACK WindowProcedure(HWND hwnd, UINT message,
                                  WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:  {
            g_statusFont = CreateFont(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                             DEFAULT_PITCH | FF_SWISS, "Arial");
            g_client = new RemoteClient(g_serverHost, g_serverPort);
            g_inputSender = new InputSender(g_client);
            g_inputHandler = new ClientInputHandler(g_client, g_inputSender);
            g_netRun = 1;
            DWORD tid = 0;
            g_netThread = CreateThread(NULL, 0, NetThreadProc, hwnd, 0, &tid);
            break;
        }

        // Bez tego przy kazdym InvalidateRect Windows czysci tlo (czarny) przed WM_PAINT = mruganie.
        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            if (g_hBitmap && g_client) {
                PaintBitmap(hdc, hwnd, ps.rcPaint);
            } else {
                RECT rect;
                GetClientRect(hwnd, &rect);
                FillRect(hdc, &rect, (HBRUSH)GetStockObject(BLACK_BRUSH));

                SetTextColor(hdc, RGB(255, 255, 255));
                SetBkMode(hdc, TRANSPARENT);

                const char* text = "Laczenie z serwerem...";
                DrawText(hdc, text, -1, &rect,
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }

            if (g_client)
                DrawStatusText(hdc, hwnd);

            EndPaint(hwnd, &ps);
            break;
        }

        case WM_DESTROY: {
            g_netRun = 0;
            if (g_client && g_client->GetInputEvent())
                SetEvent(g_client->GetInputEvent());
            if (g_client)
                g_client->Disconnect();
            if (g_netThread) {
                WaitForSingleObject(g_netThread, 4000);
                CloseHandle(g_netThread);
                g_netThread = NULL;
            }

            /* Bitmapa nalezy do RemoteClient. */
            g_hBitmap = NULL;

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

            if (g_statusFont) {
                DeleteObject(g_statusFont);
                g_statusFont = NULL;
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
                g_forceFrame = 1;
                if (g_client->GetInputEvent())
                    SetEvent(g_client->GetInputEvent());
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

    const char* klasaOkna = "RemoteViewerClass";

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

    MSG msg = {0};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
