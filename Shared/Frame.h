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
#define FRAME_FLAG_FULL       0x01  // Pe�na klatka (nie r�nica)
#define FRAME_FLAG_DIFF       0x02  // Klatka r�nicowa
#define FRAME_FLAG_COMPRESSED 0x04  // Dane skompresowane
#define FRAME_FLAG_NOCHANGE   0x08  // Brak zmian (brak payloadu)

// ===== NAG��WEK RAMKI OBRAZU =====
#pragma pack(push, 1)
struct HeaderBmp {
    char magic[3];
	char flags;
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

// DataBmp - wrapper na dane obrazu (NIE zwalnia pami�ci)
struct DataBmp {
    char* data;
    bool ownsData;  // Czy struktura jest w�a�cicielem pami�ci
    
    DataBmp() : data(NULL), ownsData(false) {
    }
    
    ~DataBmp() {
        if (data && ownsData) {
            delete[] data;
            data = NULL;
        }
    }

    void Reset() {
        if (data && ownsData) {
            delete[] data;
        }
        data = NULL;
        ownsData = false;
    }
    
    // Ustawia wska�nik BEZ przejmowania w�asno�ci
    void SetReference(char* pData) {
        if (data && ownsData) delete[] data;
        data = pData;
        ownsData = false;
    }

    void SetReferenceWithOwn(char* pData) {
        if (data && ownsData) delete[] data;
        data = pData;
        ownsData = true;
    }
    
    // Alokuje w�asn� pami�� i kopiuje
    void CopyData(const char* pData, int size) {
        if (data && ownsData) delete[] data;
        data = new char[size];
        CopyMemory(data, pData, size);
        ownsData = true;
    }
};

struct FrameBmp {
    HeaderBmp header;
    DataBmp data;
};

#endif