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
#define CMD_KEY_PRESS           12

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
    int x;
    int y;
    int wheelDelta;
    
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
    WORD virtualKey;
    WORD scanCode;
    DWORD flags;
    
    DataKey() {
        virtualKey = 0;
        scanCode = 0;
        flags = 0;
    }
};
#pragma pack(pop)

// Flagi dla HeaderBmp
#define FRAME_FLAG_FULL      0x01  // Pe³na klatka (nie ró¿nica)
#define FRAME_FLAG_DIFF      0x02  // Klatka ró¿nicowa

// ===== NAG£ÓWEK RAMKI OBRAZU =====
#pragma pack(push, 1)
struct HeaderBmp {
    char magic[3];
	BYTE flags;
    int x;
    int y;
    int width;
    int height;
    int bitsPerPixel;
    int stride;
    int length;

    HeaderBmp() {
        memcpy(magic, "BMP", 3);
		flags = 0;
        x = 0;
        y = 0;
        width = 0;
        height = 0;
        bitsPerPixel = 24;
        stride = 0;
        length = 0;
    }
};
#pragma pack(pop)

// DataBmp - wrapper na dane obrazu (NIE zwalnia pamiêci)
struct DataBmp {
    BYTE* data;
    bool ownsData;  // Czy struktura jest w³aœcicielem pamiêci
    
    DataBmp() : data(NULL), ownsData(false) {
    }
    
    ~DataBmp() {
        if (data && ownsData) {
            delete[] data;
            data = NULL;
        }
    }
    
    // Ustawia wskaŸnik BEZ przejmowania w³asnoœci
    void SetReference(BYTE* pData) {
        if (data && ownsData) delete[] data;
        data = pData;
        ownsData = false;
    }
    
    // Alokuje w³asn¹ pamiêæ i kopiuje
    void CopyData(const BYTE* pData, DWORD size) {
        if (data && ownsData) delete[] data;
        data = new BYTE[size];
        CopyMemory(data, pData, size);
        ownsData = true;
    }
};

struct FrameBmp {
    HeaderBmp header;
    DataBmp data;
};

#endif