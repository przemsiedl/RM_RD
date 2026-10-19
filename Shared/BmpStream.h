#ifndef BmpStreamH
#define BmpStreamH

#include <windows.h>

// Obraz z parametrami i danymi pikseli (do transmisji przez sieć)
struct ImageData
{
    int width;
    int height;
    int bitsPerPixel;
    int stride;
    long dataSize;
    char* pData;
    bool isFullFrame;  // true = pełny obraz, false = różnica XOR
    bool isEmptyDiff;  // true = różnica XOR jest pusta
    
    // Konstruktor - inicjalizacja
    ImageData() : width(0), height(0), bitsPerPixel(0),
                  stride(0), dataSize(0), pData(NULL),
                  isFullFrame(true), isEmptyDiff(false) {}
    
    // Destruktor - automatyczne zwalnianie
    ~ImageData() 
    { 
        if (pData) 
        {
            delete[] pData;
            pData = NULL;
        }
    }
    
    // Zabroń kopiowania (unikamy podwójnego delete)
private:
    ImageData(const ImageData&);
    ImageData& operator=(const ImageData&);
};

// Prostokat zmienionych pikseli (wspolrzedne wlaczne). any=false oznacza brak zmian.
struct FrameDirty {
    int minX;
    int minY;
    int maxX;
    int maxY;
    bool any;

    FrameDirty() : minX(0), minY(0), maxX(-1), maxY(-1), any(false) {}

    void Clear() {
        minX = 0;
        minY = 0;
        maxX = -1;
        maxY = -1;
        any = false;
    }

    void Add(int x, int y, int w, int h) {
        if (w <= 0 || h <= 0) return;
        int x2 = x + w - 1;
        int y2 = y + h - 1;
        if (!any) {
            minX = x;
            minY = y;
            maxX = x2;
            maxY = y2;
            any = true;
            return;
        }
        if (x < minX) minX = x;
        if (y < minY) minY = y;
        if (x2 > maxX) maxX = x2;
        if (y2 > maxY) maxY = y2;
    }

    void SetFull(int w, int h) {
        if (w <= 0 || h <= 0) {
            Clear();
            return;
        }
        minX = 0;
        minY = 0;
        maxX = w - 1;
        maxY = h - 1;
        any = true;
    }

    int Width() const { return any ? (maxX - minX + 1) : 0; }
    int Height() const { return any ? (maxY - minY + 1) : 0; }
};

// Wynik PackChangedBlocks (XOR bloczkow bez pelnego bufora diff).
#define BLOCK_PACK_ERROR    0
#define BLOCK_PACK_NOCHANGE 1
#define BLOCK_PACK_OK       2
#define BLOCK_PACK_TOO_MUCH 3

class BmpStream
{
private: 
    // Bufory obrazów — przydzial trwa miedzy klatkami, Capture je zamienia.
    ImageData* pPrevious;
    ImageData* pCurrent;
    ImageData* pDiff;  // obliczona różnica
    
    // Preferowane BPP (16, 24 albo 32)
    int bitsPerPixel;

    // Powierzchnia GDI trzymana miedzy klatkami (bez CreateDIBSection co klatke).
    HDC cachedScreenDC;
    HDC cachedMemDC;
    HBITMAP cachedDib;
    HBITMAP cachedOldBmp;
    void* cachedBits;
    int cachedW;
    int cachedH;
    int cachedBpp;

    bool EnsureSurface(int screenWidth, int screenHeight);
    void ReleaseSurface();
    
    // Zabroń kopiowania
    BmpStream(const BmpStream&);
    BmpStream& operator=(const BmpStream&);

public:
    BmpStream(int bpp = 24);
    ~BmpStream();
    
    void Reset();
    bool Capture();

    // Oblicza diff dla regionu (x, y, w, h). Cały obraz: (0, 0, width, height).
    bool CalcDiff(int x, int y, int w, int h);

    // Zwraca obliczoną różnicę (NIE zwalniać - zarządzane przez klasę)
    const ImageData* GetDiff() const { return pDiff; }

    int GetWidth() const { return pCurrent ? pCurrent->width : 0; }
    int GetHeight() const { return pCurrent ? pCurrent->height : 0; }

    // Kopiuje region (x, y, w, h) z current do out. Cały obraz: (0, 0, width, height).
    bool CopyCurrentTo(ImageData* out, int x, int y, int w, int h) const;

    // ===== SKLADANIE OBRAZOW (REKONSTRUKCJA Z ROZNIC) =====

    // Aplikuje diff na target. diffWidth x diffHeight z diffData do target w (targetX, targetY).
    static bool ApplyDiff(char* target, int targetStride, int targetBpp,
        const char* diffData, int diffStride, int diffWidth, int diffHeight,
        int targetX = 0, int targetY = 0, bool isFullFrame = false,
        FrameDirty* dirty = NULL);

    // Oblicza diff = current XOR previous dla regionu (rx,ry,rw,rh). Cały obraz: (0,0,width,height).
    static bool ComputeDiff(const ImageData* current, const ImageData* previous,
        ImageData* outDiff, int rx, int ry, int rw, int rh);

    // Bounding rect niezerowych pikseli w regionie (sx,sy,sw,sh) diffa. Cały: (0,0,width,height).
    static bool ComputeDiffBoundingRect(const ImageData* diff,
        int& outMinX, int& outMinY, int& outMaxX, int& outMaxY,
        int sx, int sy, int sw, int sh);

    // Siatka bloczkow na XOR-diff (wykrywanie ruchu): tylko komórki z niezerowym XOR.
    // maxDirtyAreaRatio np. 0.7f — przy wiekszym % zmienionych pikseli zwroc false (jeden blob calosci).
    // *outBuf alokowane malloc; przy sukcesie *outLen > 0.
    static bool PackDiffAsBlocks(const ImageData* diff, int blockW, int blockH,
        float maxDirtyAreaRatio, char** outBuf, int* outLen);

    // Odwrotność: zastosuj payload BLOCKS na istniejącym buforze obrazu klienta.
    // dirty (opcjonalnie) dostaje sume prostokatow bloczkow.
    static bool ApplyPackedBlockDiffs(ImageData* img, const char* payload, int payloadLen,
        FrameDirty* dirty = NULL);

    // Udzial powierzchni komorek siatki, ktore roznia sie miedzy klatkami (0..1).
    // Przy >= 70% zmian zwraca 1, zeby nie skanowac reszty ekranu.
    static float MeasureDirtyRatio(const ImageData* current, const ImageData* previous,
        int blockW, int blockH);

    // XOR tylko zmienionych komorek, od razu do payloadu BLOCKS (bez bufora calej klatki).
    // outBuf: new[] — wywolujacy zwalnia delete[].
    static int PackChangedBlocks(const ImageData* current, const ImageData* previous,
        int blockW, int blockH, float maxDirtyAreaRatio, char** outBuf, int* outLen);

    // Próba lepszego diffa przez globalne (dx,dy): outDiff = current XOR shifted(previous); OOB → 0.
    // Zwraca true, jeśli sensownie mniej bajtów różnych niż zwykły XOR (ComputeDiff).
    static bool TryGlobalTranslationDiff(const ImageData* current, const ImageData* previous,
        ImageData* outDiff, int* outDx, int* outDy);

    // Klient: nowy obraz = (stary przesunięty o (dx,dy), poza obrazem 0) XOR xorPlane; xorPlane==NULL → XOR 0.
    static bool ApplyTranslationResidual(ImageData* img, int dx, int dy,
        const char* xorPlane, int xorStride, int xorW, int xorH);
};

#endif