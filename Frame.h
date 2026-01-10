#ifndef FrameH
#define FrameH

#include <windows.h>

// ===== KOMENDY =====
#define CMD_GET_FRAME           1
#define CMD_MOUSE_MOVE          2
#define CMD_MOUSE_LEFT_DOWN     3
#define CMD_MOUSE_LEFT_UP       4
#define CMD_MOUSE_RIGHT_DOWN    5
#define CMD_MOUSE_RIGHT_UP      6
#define CMD_MOUSE_MIDDLE_DOWN   7
#define CMD_MOUSE_MIDDLE_UP     8
#define CMD_MOUSE_WHEEL         9
#define CMD_KEY_DOWN            10
#define CMD_KEY_UP              11
#define CMD_KEY_PRESS           12  // Down + Up

// ===== RAMKA KOMENDY =====
#pragma pack(push, 1)
struct FrameCmd {
    char magic[3];
    int cmd;
    int length;

    FrameCmd() {
        memcpy(magic, "CMD", 3);
        cmd = 0;
        length = 0;
    }
};
#pragma pack(pop)

// ===== DANE KOMENDY MYSZY =====
#pragma pack(push, 1)
struct DataMouse {
    int x;              // Pozycja X (wspó³rzêdne ekranu)
    int y;              // Pozycja Y
    int wheelDelta;     // Delta kó³ka myszy (dla CMD_MOUSE_WHEEL)
    
    DataMouse() {
        x = 0;
        y = 0;
        wheelDelta = 0;
    }
};
#pragma pack(pop)

// ===== DANE KOMENDY KLAWIATURY =====
#pragma pack(push, 1)
struct DataKey {
    WORD virtualKey;    // Kod Virtual Key (VK_*)
    WORD scanCode;      // Scan code
    DWORD flags;        // Flagi (KEYEVENTF_*)
    
    DataKey() {
        virtualKey = 0;
        scanCode = 0;
        flags = 0;
    }
};
#pragma pack(pop)

// ===== NAG£ÓWEK RAMKI OBRAZU =====
#pragma pack(push, 1)
struct HeaderBmp {
    char magic[3];
    int x;
    int y;
    int width;
    int height;
    int length;

    HeaderBmp() {
        memcpy(magic, "BMP", 3);
        x = 0;
        y = 0;
        width = 0;
        height = 0;
        length = 0;
    }
};
#pragma pack(pop)

struct DataBmp {
    char* data;

    DataBmp() :  data(NULL) {
    }
    
    ~DataBmp() {
        if (data) {
            delete[] data;
            data = NULL;
        }
    }
};

struct FrameBmp {
    HeaderBmp header;
    DataBmp data;
};

#endif
